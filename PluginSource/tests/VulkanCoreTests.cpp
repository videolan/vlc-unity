#include "../vulkan/VulkanUnityCopyCore.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

int failures = 0;
int barrierCount = 0;
int copyCount = 0;
VkImageMemoryBarrier barriers[2];

template <typename T>
T handle(uintptr_t value)
{
    return reinterpret_cast<T>(value);
}

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeCmdPipelineBarrier(
    VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
    VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t,
    const VkBufferMemoryBarrier*, uint32_t imageBarrierCount,
    const VkImageMemoryBarrier* imageBarriers)
{
    for (uint32_t i = 0; i < imageBarrierCount && barrierCount < 2; ++i)
        barriers[barrierCount++] = imageBarriers[i];
}

VKAPI_ATTR void VKAPI_CALL fakeCmdCopyImage(
    VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
    uint32_t regionCount, const VkImageCopy* regions)
{
    if (regionCount == 1 && regions && regions[0].extent.width == 640 &&
        regions[0].extent.height == 360) {
        ++copyCount;
    }
}

class FakeFrameSource final : public IVulkanFrameSource
{
public:
    bool acquireReadyFrame(VulkanExternalFrame& out) override
    {
        if (!ready)
            return false;
        out = frame;
        return true;
    }
    void onCopyDeferred(uint64_t token) override
    {
        check(token == frame.token, "deferred token must match");
        ++deferred;
    }
    bool onCopyScheduled(uint64_t token) override
    {
        check(token == frame.token, "scheduled token must match");
        ++scheduled;
        ready = false;
        return true;
    }
    void onCopyComplete(uint64_t token) override
    {
        check(token == frame.token, "completed token must match");
        ++completed;
    }
    void onCopyFailed(uint64_t) override { ++failed; }

    VulkanExternalFrame frame;
    bool ready = true;
    int scheduled = 0;
    int completed = 0;
    int failed = 0;
    int deferred = 0;
};

class FakeSubmission final : public IVulkanSubmissionStrategy
{
public:
    bool begin(const VulkanExternalFrame&, VkCommandBuffer& commandBuffer) override
    {
        commandBuffer = handle<VkCommandBuffer>(3);
        ++begins;
        return true;
    }
    bool endAndSubmit(const VulkanExternalFrame&) override
    {
        ++submits;
        return true;
    }
    void cancel(const VulkanExternalFrame&) override { ++cancels; }
    size_t collectCompletions(VulkanSubmissionCompletion*, size_t) override
    {
        return 0;
    }
    bool completesImmediately() const override { return true; }
    bool hasInFlightWork() const override { return false; }
    void shutdown() override {}

    int begins = 0;
    int submits = 0;
    int cancels = 0;
};

IUnityGraphicsVulkan fakeGraphics;
int ensureOutsideCalls = 0;
int accessTextureCalls = 0;

void UNITY_INTERFACE_API fakeEnsureOutside()
{
    ++ensureOutsideCalls;
}

bool UNITY_INTERFACE_API fakeAccessTexture(
    void*, const VkImageSubresource*, VkImageLayout layout,
    VkPipelineStageFlags stage, VkAccessFlags access,
    UnityVulkanResourceAccessMode mode, UnityVulkanImage* image)
{
    ++accessTextureCalls;
    check(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "destination layout must be transfer destination");
    check(stage == VK_PIPELINE_STAGE_TRANSFER_BIT,
          "destination stage must be transfer");
    check(access == VK_ACCESS_TRANSFER_WRITE_BIT,
          "destination access must be transfer write");
    check(mode == kUnityVulkanResourceAccess_PipelineBarrier,
          "destination must use Unity pipeline-barrier tracking");
    memset(image, 0, sizeof(*image));
    image->image = handle<VkImage>(2);
    image->format = VK_FORMAT_R8G8B8A8_UNORM;
    image->extent = { 800, 600, 1 };
    return true;
}

void testFormatMismatchCancelsRecording()
{
    FakeFrameSource source;
    source.frame.image = handle<VkImage>(1);
    source.frame.format = VK_FORMAT_B8G8R8A8_UNORM;
    source.frame.extent = { 64, 64, 1 };
    source.frame.layout = VK_IMAGE_LAYOUT_GENERAL;
    source.frame.token = 7;
    FakeSubmission submission;
    VulkanCopyCommandFunctions functions;
    functions.cmdPipelineBarrier = fakeCmdPipelineBarrier;
    functions.cmdCopyImage = fakeCmdCopyImage;
    VulkanUnityCopyCore core;
    check(core.initialize(&fakeGraphics, &source, &submission, functions),
          "format mismatch fixture must initialize");
    check(core.setUnityTexture(reinterpret_cast<void*>(4)),
          "format mismatch fixture needs a destination");
    core.performRenderThreadWork();
    check(submission.cancels == 1 && source.failed == 1,
          "an incompatible copy format must cancel recording and fail cleanly");
}

void testTextureRequiresInitializedCore()
{
    VulkanUnityCopyCore core;
    check(!core.setUnityTexture(reinterpret_cast<void*>(4)),
          "destination texture must be rejected before initialization");

    FakeFrameSource source;
    FakeSubmission submission;
    VulkanCopyCommandFunctions functions;
    functions.cmdPipelineBarrier = fakeCmdPipelineBarrier;
    functions.cmdCopyImage = fakeCmdCopyImage;
    check(core.initialize(&fakeGraphics, &source, &submission, functions),
          "readiness fixture must initialize");
    check(core.initialized() &&
              core.setUnityTexture(reinterpret_cast<void*>(4)),
          "initialized core must accept a destination texture");
    core.shutdown();
    check(!core.initialized() &&
              !core.setUnityTexture(reinterpret_cast<void*>(4)),
          "shutdown core must reject stale destination setup");
}

void testPlatformFreeCore()
{
    memset(&fakeGraphics, 0, sizeof(fakeGraphics));
    fakeGraphics.EnsureOutsideRenderPass = fakeEnsureOutside;
    fakeGraphics.AccessTexture = fakeAccessTexture;

    FakeFrameSource source;
    source.frame.image = handle<VkImage>(1);
    source.frame.format = VK_FORMAT_R8G8B8A8_UNORM;
    source.frame.extent = { 640, 360, 1 };
    source.frame.layout = VK_IMAGE_LAYOUT_GENERAL;
    source.frame.releaseLayout = VK_IMAGE_LAYOUT_GENERAL;
    source.frame.sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    source.frame.sourceAccess = VK_ACCESS_MEMORY_WRITE_BIT;
    source.frame.sourceQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
    source.frame.copyQueueFamily = 7;
    source.frame.releaseQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
    source.frame.acquireOwnership = true;
    source.frame.releaseOwnership = true;
    source.frame.token = 42;

    FakeSubmission submission;
    VulkanCopyCommandFunctions functions;
    functions.cmdPipelineBarrier = fakeCmdPipelineBarrier;
    functions.cmdCopyImage = fakeCmdCopyImage;

    VulkanUnityCopyCore core;
    check(core.initialize(&fakeGraphics, &source, &submission, functions),
          "core initialization must pass with complete fake contracts");
    check(core.setUnityTexture(reinterpret_cast<void*>(4)),
          "Unity texture must be accepted");
    core.performRenderThreadWork();

    check(ensureOutsideCalls == 1, "core must leave the render pass once");
    check(accessTextureCalls == 1, "core must access the Unity texture once");
    check(submission.begins == 1 && submission.submits == 1,
          "fake strategy must be used for command provenance and completion");
    check(source.scheduled == 1 && source.completed == 1 && source.failed == 0,
          "immediate strategy must publish one completed source cycle");
    check(copyCount == 1, "copy must be recorded exactly once");
    check(barrierCount == 2, "foreign acquire and release barriers are required");
    check(barriers[0].srcQueueFamilyIndex == VK_QUEUE_FAMILY_FOREIGN_EXT &&
          barriers[0].dstQueueFamilyIndex == 7,
          "acquire barrier must transfer foreign ownership to Unity's queue");
    check(barriers[1].srcQueueFamilyIndex == 7 &&
          barriers[1].dstQueueFamilyIndex == VK_QUEUE_FAMILY_FOREIGN_EXT,
          "release barrier must transfer ownership back to foreign");
}

void testSharedExternalImageHelpers()
{
    check(kVulkanFrameFormat == VK_FORMAT_R8G8B8A8_UNORM,
          "frame sources must copy from RGBA8");
    check(VulkanFirstMemoryTypeBit(0) == UINT32_MAX,
          "an empty memory-type mask must report no candidate");
    check(VulkanFirstMemoryTypeBit(1) == 0,
          "bit zero must remain selectable");
    check(VulkanFirstMemoryTypeBit(0x10) == 4,
          "memory-type selection must return the first set bit");

    VkExternalMemoryImageCreateInfo external = {};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    VkImageCreateInfo info = {};
    VulkanInitExternalImageInfo(
        info, 64, 32, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        &external);
    check(info.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO &&
              info.pNext == &external &&
              info.imageType == VK_IMAGE_TYPE_2D &&
              info.format == kVulkanFrameFormat &&
              info.extent.width == 64 &&
              info.extent.height == 32 &&
              info.extent.depth == 1 &&
              info.mipLevels == 1 &&
              info.arrayLayers == 1 &&
              info.samples == VK_SAMPLE_COUNT_1_BIT &&
              info.tiling == VK_IMAGE_TILING_OPTIMAL &&
              info.usage == (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT) &&
              info.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED,
          "shared image-info setup must fill the external frame contract");
}

} // namespace

int main()
{
    testPlatformFreeCore();
    testFormatMismatchCancelsRecording();
    testTextureRequiresInitializedCore();
    testSharedExternalImageHelpers();
    if (failures)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
