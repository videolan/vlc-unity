#include "VulkanUnityCopyCore.h"
#include "Log.h"

#include <algorithm>
#include <cstring>

const VkFormat kVulkanFrameFormat = VK_FORMAT_R8G8B8A8_UNORM;

bool VulkanUnityCopyCore::initialize(IUnityGraphicsVulkan* graphics,
                                     IVulkanFrameSource* frameSource,
                                     IVulkanSubmissionStrategy* submission,
                                     const VulkanCopyCommandFunctions& functions)
{
    if (!graphics || !frameSource || !submission ||
        !functions.cmdPipelineBarrier || !functions.cmdCopyImage) {
        DEBUG("[Vulkan] copy core initialization received an incomplete dependency");
        return false;
    }

    m_graphics = graphics;
    m_frameSource = frameSource;
    m_submission = submission;
    m_functions = functions;
    return true;
}

void VulkanUnityCopyCore::shutdown()
{
    if (m_submission)
        m_submission->shutdown();
    m_unityTexture = nullptr;
    m_graphics = nullptr;
    m_frameSource = nullptr;
    m_submission = nullptr;
    m_functions = {};
}

bool VulkanUnityCopyCore::setUnityTexture(void* unityTexture)
{
    if (!initialized() || !unityTexture) {
        DEBUG("[Vulkan] refusing a destination texture before copy-core initialization or with a null handle");
        return false;
    }
    m_unityTexture = unityTexture;
    return true;
}

void VulkanUnityCopyCore::pollCompletions()
{
    if (!m_submission || !m_frameSource)
        return;

    VulkanSubmissionCompletion completions[3];
    const size_t count = m_submission->collectCompletions(
        completions, sizeof(completions) / sizeof(completions[0]));
    for (size_t i = 0; i < count; ++i) {
        if (completions[i].status == VulkanSubmissionCompletionStatus::Complete)
            m_frameSource->onCopyComplete(completions[i].token);
        else
            m_frameSource->onCopyFailed(completions[i].token);
    }
}

void VulkanUnityCopyCore::performRenderThreadWork()
{
    pollCompletions();

    if (!m_graphics || !m_frameSource || !m_submission || !m_unityTexture)
        return;

    VulkanExternalFrame source;
    if (!m_frameSource->acquireReadyFrame(source))
        return;

    m_graphics->EnsureOutsideRenderPass();

    UnityVulkanImage destination;
    memset(&destination, 0, sizeof(destination));
    if (!m_graphics->AccessTexture(m_unityTexture,
                                   UnityVulkanWholeImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_ACCESS_TRANSFER_WRITE_BIT,
                                   kUnityVulkanResourceAccess_PipelineBarrier,
                                   &destination) ||
        destination.image == VK_NULL_HANDLE) {
        DEBUG("[Vulkan] Unity destination AccessTexture failed");
        m_frameSource->onCopyDeferred(source.token);
        return;
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!m_submission->begin(source, commandBuffer) ||
        commandBuffer == VK_NULL_HANDLE) {
        m_frameSource->onCopyDeferred(source.token);
        return;
    }

    if (!RecordCopy(commandBuffer, source, destination, m_functions)) {
        m_submission->cancel(source);
        m_frameSource->onCopyFailed(source.token);
        return;
    }
    if (!m_frameSource->onCopyScheduled(source.token)) {
        m_submission->cancel(source);
        m_frameSource->onCopyDeferred(source.token);
        return;
    }
    if (!m_submission->endAndSubmit(source)) {
        m_submission->cancel(source);
        m_frameSource->onCopyFailed(source.token);
        return;
    }

    if (m_submission->completesImmediately())
        m_frameSource->onCopyComplete(source.token);
}

bool VulkanUnityCopyCore::hasInFlightWork() const
{
    return m_submission && m_submission->hasInFlightWork();
}

bool VulkanUnityCopyCore::RecordCopy(VkCommandBuffer commandBuffer,
                                     const VulkanExternalFrame& source,
                                     const UnityVulkanImage& destination,
                                     const VulkanCopyCommandFunctions& functions)
{
    if (commandBuffer == VK_NULL_HANDLE || source.image == VK_NULL_HANDLE ||
        destination.image == VK_NULL_HANDLE || source.extent.width == 0 ||
        source.extent.height == 0 || !functions.cmdPipelineBarrier ||
        !functions.cmdCopyImage) {
        return false;
    }
    if (source.format != VK_FORMAT_UNDEFINED &&
        destination.format != VK_FORMAT_UNDEFINED &&
        source.format != destination.format) {
        return false;
    }

    VkImageMemoryBarrier acquire = {};
    acquire.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    acquire.srcAccessMask = source.sourceAccess;
    acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    acquire.oldLayout = source.layout;
    acquire.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    acquire.srcQueueFamilyIndex = source.acquireOwnership
        ? source.sourceQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    acquire.dstQueueFamilyIndex = source.acquireOwnership
        ? source.copyQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    acquire.image = source.image;
    acquire.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    functions.cmdPipelineBarrier(commandBuffer,
                                 source.sourceStage,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &acquire);

    VkImageCopy region = {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent.width = std::min(source.extent.width, destination.extent.width);
    region.extent.height = std::min(source.extent.height, destination.extent.height);
    region.extent.depth = 1;
    if (region.extent.width == 0 || region.extent.height == 0)
        return false;

    functions.cmdCopyImage(commandBuffer,
                           source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    if (source.releaseOwnership) {
        VkImageMemoryBarrier release = {};
        release.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        release.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        release.dstAccessMask = 0;
        release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        release.newLayout = source.releaseLayout;
        release.srcQueueFamilyIndex = source.copyQueueFamily;
        release.dstQueueFamilyIndex = source.releaseQueueFamily;
        release.image = source.image;
        release.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        functions.cmdPipelineBarrier(commandBuffer,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &release);
    }

    return true;
}
