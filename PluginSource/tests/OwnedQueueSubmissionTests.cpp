#include "../OwnedQueueSubmissionStrategy.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

int failures = 0;
int submitCount = 0;
int destroyFenceCount = 0;
int destroyPoolCount = 0;
int accessQueueCalls = 0;
bool fenceSignaled[64] = {};
bool signalSubmittedFence = true;
VkResult queueSubmitResult = VK_SUCCESS;
VkFence lastSubmittedFence = VK_NULL_HANDLE;

void UNITY_INTERFACE_API forbiddenAccessQueue(
    UnityRenderingEventAndData, int, void*, bool)
{
    ++accessQueueCalls;
}

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

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateCommandPool(
    VkDevice, const VkCommandPoolCreateInfo* info,
    const VkAllocationCallbacks*, VkCommandPool* pool)
{
    check(info->queueFamilyIndex == 4,
          "command pool must use Unity graphics queue family");
    *pool = handle<VkCommandPool>(1);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyCommandPool(
    VkDevice, VkCommandPool, const VkAllocationCallbacks*)
{
    ++destroyPoolCount;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeAllocateCommandBuffers(
    VkDevice, const VkCommandBufferAllocateInfo* info,
    VkCommandBuffer* buffers)
{
    check(info->commandBufferCount == 3,
          "three command buffers must be preallocated");
    for (uint32_t i = 0; i < info->commandBufferCount; ++i)
        buffers[i] = handle<VkCommandBuffer>(10 + i);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags)
{
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeBeginCommandBuffer(
    VkCommandBuffer, const VkCommandBufferBeginInfo*)
{
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeEndCommandBuffer(VkCommandBuffer)
{
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFence(
    VkDevice, const VkFenceCreateInfo* info,
    const VkAllocationCallbacks*, VkFence* fence)
{
    static uintptr_t next = 1;
    check((info->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0,
          "preallocated fences must begin signaled");
    *fence = handle<VkFence>(next++);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(
    VkDevice, VkFence, const VkAllocationCallbacks*)
{
    ++destroyFenceCount;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetFences(
    VkDevice, uint32_t count, const VkFence* fences)
{
    for (uint32_t i = 0; i < count; ++i)
        fenceSignaled[reinterpret_cast<uintptr_t>(fences[i])] = false;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetFenceStatus(VkDevice, VkFence fence)
{
    return fenceSignaled[reinterpret_cast<uintptr_t>(fence)]
        ? VK_SUCCESS : VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeQueueSubmit(
    VkQueue, uint32_t count, const VkSubmitInfo* submits, VkFence fence)
{
    check(count == 1, "Linux must issue one submit per copied frame");
    check(submits[0].waitSemaphoreCount == 1 &&
          submits[0].signalSemaphoreCount == 1 &&
          submits[0].commandBufferCount == 1,
          "submit must carry exactly one wait, command buffer, and signal");
    ++submitCount;
    lastSubmittedFence = fence;
    if (signalSubmittedFence && queueSubmitResult == VK_SUCCESS)
        fenceSignaled[reinterpret_cast<uintptr_t>(fence)] = true;
    return queueSubmitResult;
}

void testSubmissionLifecycle()
{
    IUnityGraphicsVulkan graphics;
    memset(&graphics, 0, sizeof(graphics));
    graphics.AccessQueue = forbiddenAccessQueue;
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    instance.graphicsQueue = handle<VkQueue>(2);
    instance.queueFamilyIndex = 4;

    OwnedQueueVulkanFunctions functions;
    functions.createCommandPool = fakeCreateCommandPool;
    functions.destroyCommandPool = fakeDestroyCommandPool;
    functions.allocateCommandBuffers = fakeAllocateCommandBuffers;
    functions.resetCommandBuffer = fakeResetCommandBuffer;
    functions.beginCommandBuffer = fakeBeginCommandBuffer;
    functions.endCommandBuffer = fakeEndCommandBuffer;
    functions.createFence = fakeCreateFence;
    functions.destroyFence = fakeDestroyFence;
    functions.resetFences = fakeResetFences;
    functions.getFenceStatus = fakeGetFenceStatus;
    functions.queueSubmit = fakeQueueSubmit;

    OwnedQueueSubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "owned queue strategy must preallocate successfully");
    VulkanExternalFrame frame;
    frame.token = 42;
    frame.sourceReady = handle<VkSemaphore>(20);
    frame.copyComplete = handle<VkSemaphore>(21);
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(strategy.begin(frame, commandBuffer) && commandBuffer != VK_NULL_HANDLE,
          "free slot must begin recording without allocation");
    check(strategy.endAndSubmit(frame),
          "recorded slot must wait for the queue-authorized event");
    VkCommandBuffer second = VK_NULL_HANDLE;
    check(!strategy.begin(frame, second),
          "pending slot must apply nonblocking back-pressure");
    check(submitCount == 0,
          "recording event must not submit before queue access is granted");
    check(accessQueueCalls == 0,
          "submission must not register an asynchronous AccessQueue callback");
    strategy.submitPendingQueueWork();
    check(submitCount == 1, "queue-authorized event must submit exactly once");

    VulkanSubmissionCompletion completion;
    check(strategy.collectCompletions(&completion, 1) == 1,
          "signaled fence must recycle nonblockingly");
    check(completion.token == 42 &&
          completion.status == VulkanSubmissionCompletionStatus::Complete,
          "completion must retain the opaque slot token");
    check(!strategy.hasInFlightWork(),
          "completed slot must no longer be in flight");
    strategy.shutdown();
    check(destroyFenceCount == 3 && destroyPoolCount == 1,
          "all preallocated submission resources must be destroyed");
}

void testThreeSlotNonblockingBackPressure()
{
    IUnityGraphicsVulkan graphics;
    memset(&graphics, 0, sizeof(graphics));
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    instance.graphicsQueue = handle<VkQueue>(2);
    instance.queueFamilyIndex = 4;

    OwnedQueueVulkanFunctions functions;
    functions.createCommandPool = fakeCreateCommandPool;
    functions.destroyCommandPool = fakeDestroyCommandPool;
    functions.allocateCommandBuffers = fakeAllocateCommandBuffers;
    functions.resetCommandBuffer = fakeResetCommandBuffer;
    functions.beginCommandBuffer = fakeBeginCommandBuffer;
    functions.endCommandBuffer = fakeEndCommandBuffer;
    functions.createFence = fakeCreateFence;
    functions.destroyFence = fakeDestroyFence;
    functions.resetFences = fakeResetFences;
    functions.getFenceStatus = fakeGetFenceStatus;
    functions.queueSubmit = fakeQueueSubmit;

    OwnedQueueSubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "back-pressure fixture must initialize");
    for (uint64_t token = 100; token < 103; ++token) {
        VulkanExternalFrame frame;
        frame.token = token;
        frame.sourceReady = handle<VkSemaphore>(token + 100);
        frame.copyComplete = handle<VkSemaphore>(token + 200);
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        check(strategy.begin(frame, commandBuffer),
              "each preallocated command slot must be acquired without allocation");
    }
    VulkanExternalFrame saturated;
    saturated.token = 999;
    saturated.sourceReady = handle<VkSemaphore>(999);
    saturated.copyComplete = handle<VkSemaphore>(1000);
    VkCommandBuffer unavailable = VK_NULL_HANDLE;
    check(!strategy.begin(saturated, unavailable),
          "a full command ring must defer instead of waiting");
    check(strategy.hasInFlightWork(),
          "recording command slots count as in-flight lifecycle work");
    for (uint64_t token = 100; token < 103; ++token) {
        VulkanExternalFrame frame;
        frame.token = token;
        strategy.cancel(frame);
    }
    check(!strategy.hasInFlightWork(),
          "cancelled recordings must be released without a CPU wait");
    strategy.shutdown();
}

void testFencePollingAndDeferredShutdown()
{
    IUnityGraphicsVulkan graphics;
    memset(&graphics, 0, sizeof(graphics));
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    instance.graphicsQueue = handle<VkQueue>(2);
    instance.queueFamilyIndex = 4;
    OwnedQueueVulkanFunctions functions;
    functions.createCommandPool = fakeCreateCommandPool;
    functions.destroyCommandPool = fakeDestroyCommandPool;
    functions.allocateCommandBuffers = fakeAllocateCommandBuffers;
    functions.resetCommandBuffer = fakeResetCommandBuffer;
    functions.beginCommandBuffer = fakeBeginCommandBuffer;
    functions.endCommandBuffer = fakeEndCommandBuffer;
    functions.createFence = fakeCreateFence;
    functions.destroyFence = fakeDestroyFence;
    functions.resetFences = fakeResetFences;
    functions.getFenceStatus = fakeGetFenceStatus;
    functions.queueSubmit = fakeQueueSubmit;

    const int fencesBefore = destroyFenceCount;
    const int poolsBefore = destroyPoolCount;
    signalSubmittedFence = false;
    queueSubmitResult = VK_SUCCESS;
    OwnedQueueSubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "deferred-shutdown fixture must initialize");
    VulkanExternalFrame frame;
    frame.token = 77;
    frame.sourceReady = handle<VkSemaphore>(30);
    frame.copyComplete = handle<VkSemaphore>(31);
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(strategy.begin(frame, commandBuffer) && strategy.endAndSubmit(frame),
          "deferred-shutdown frame must reach queue access");
    strategy.submitPendingQueueWork();
    VulkanSubmissionCompletion completion;
    check(strategy.collectCompletions(&completion, 1) == 0,
          "unsignaled fence status must return immediately without completion");
    strategy.shutdown();
    check(destroyFenceCount == fencesBefore && destroyPoolCount == poolsBefore,
          "shutdown must defer resource destruction while a fence is unsignaled");
    fenceSignaled[reinterpret_cast<uintptr_t>(lastSubmittedFence)] = true;
    check(strategy.collectCompletions(&completion, 1) == 1 &&
          completion.token == 77,
          "later nonblocking fence status must publish the original token");
    strategy.shutdown();
    check(destroyFenceCount == fencesBefore + 3 &&
          destroyPoolCount == poolsBefore + 1,
          "deferred shutdown must deterministically destroy the fixed ring");
    signalSubmittedFence = true;
}

void testQueueSubmissionFailure()
{
    IUnityGraphicsVulkan graphics;
    memset(&graphics, 0, sizeof(graphics));
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    instance.graphicsQueue = handle<VkQueue>(2);
    instance.queueFamilyIndex = 4;
    OwnedQueueVulkanFunctions functions;
    functions.createCommandPool = fakeCreateCommandPool;
    functions.destroyCommandPool = fakeDestroyCommandPool;
    functions.allocateCommandBuffers = fakeAllocateCommandBuffers;
    functions.resetCommandBuffer = fakeResetCommandBuffer;
    functions.beginCommandBuffer = fakeBeginCommandBuffer;
    functions.endCommandBuffer = fakeEndCommandBuffer;
    functions.createFence = fakeCreateFence;
    functions.destroyFence = fakeDestroyFence;
    functions.resetFences = fakeResetFences;
    functions.getFenceStatus = fakeGetFenceStatus;
    functions.queueSubmit = fakeQueueSubmit;

    queueSubmitResult = VK_ERROR_DEVICE_LOST;
    OwnedQueueSubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "queue failure fixture must initialize");
    VulkanExternalFrame frame;
    frame.token = 88;
    frame.sourceReady = handle<VkSemaphore>(40);
    frame.copyComplete = handle<VkSemaphore>(41);
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(strategy.begin(frame, commandBuffer) && strategy.endAndSubmit(frame),
          "queue failure fixture must reach the asynchronous callback");
    strategy.submitPendingQueueWork();
    VulkanSubmissionCompletion completion;
    check(strategy.collectCompletions(&completion, 1) == 1 &&
          completion.token == 88 &&
          completion.status == VulkanSubmissionCompletionStatus::Failed,
          "queue-submit failure must be reported once and recycle the slot");
    strategy.shutdown();
    queueSubmitResult = VK_SUCCESS;
}

void testDeviceShutdownAbandonsUnfinishedObjects()
{
    IUnityGraphicsVulkan graphics;
    memset(&graphics, 0, sizeof(graphics));
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    instance.graphicsQueue = handle<VkQueue>(2);
    instance.queueFamilyIndex = 4;
    OwnedQueueVulkanFunctions functions;
    functions.createCommandPool = fakeCreateCommandPool;
    functions.destroyCommandPool = fakeDestroyCommandPool;
    functions.allocateCommandBuffers = fakeAllocateCommandBuffers;
    functions.resetCommandBuffer = fakeResetCommandBuffer;
    functions.beginCommandBuffer = fakeBeginCommandBuffer;
    functions.endCommandBuffer = fakeEndCommandBuffer;
    functions.createFence = fakeCreateFence;
    functions.destroyFence = fakeDestroyFence;
    functions.resetFences = fakeResetFences;
    functions.getFenceStatus = fakeGetFenceStatus;
    functions.queueSubmit = fakeQueueSubmit;

    const int fencesBefore = destroyFenceCount;
    const int poolsBefore = destroyPoolCount;
    signalSubmittedFence = false;
    OwnedQueueSubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "device-shutdown abandonment fixture must initialize");
    VulkanExternalFrame frame;
    frame.token = 99;
    frame.sourceReady = handle<VkSemaphore>(50);
    frame.copyComplete = handle<VkSemaphore>(51);
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(strategy.begin(frame, commandBuffer) && strategy.endAndSubmit(frame),
          "device-shutdown frame must reach queue access");
    strategy.submitPendingQueueWork();
    check(strategy.hasInFlightWork(),
          "device-shutdown fixture needs an unfinished fence");
    strategy.abandonForDeviceShutdown();
    check(!strategy.hasInFlightWork(),
          "abandonment must detach all callbacks and CPU lifecycle state");
    strategy.shutdown();
    check(destroyFenceCount == fencesBefore && destroyPoolCount == poolsBefore,
          "in-use Vulkan objects must be left to imminent VkDevice teardown");
    signalSubmittedFence = true;
}

void testPendingQueueAccessSlotIsReclaimedAtShutdown()
{
    IUnityGraphicsVulkan graphics;
    memset(&graphics, 0, sizeof(graphics));
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    instance.graphicsQueue = handle<VkQueue>(2);
    instance.queueFamilyIndex = 4;
    OwnedQueueVulkanFunctions functions;
    functions.createCommandPool = fakeCreateCommandPool;
    functions.destroyCommandPool = fakeDestroyCommandPool;
    functions.allocateCommandBuffers = fakeAllocateCommandBuffers;
    functions.resetCommandBuffer = fakeResetCommandBuffer;
    functions.beginCommandBuffer = fakeBeginCommandBuffer;
    functions.endCommandBuffer = fakeEndCommandBuffer;
    functions.createFence = fakeCreateFence;
    functions.destroyFence = fakeDestroyFence;
    functions.resetFences = fakeResetFences;
    functions.getFenceStatus = fakeGetFenceStatus;
    functions.queueSubmit = fakeQueueSubmit;

    const int fencesBefore = destroyFenceCount;
    const int poolsBefore = destroyPoolCount;
    const int submitsBefore = submitCount;
    OwnedQueueSubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "stranded-slot fixture must initialize");
    VulkanExternalFrame frame;
    frame.token = 123;
    frame.sourceReady = handle<VkSemaphore>(60);
    frame.copyComplete = handle<VkSemaphore>(61);
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(strategy.begin(frame, commandBuffer) && strategy.endAndSubmit(frame),
          "stranded-slot fixture must reach PendingQueueAccess");
    check(strategy.hasInFlightWork(),
          "a granted-but-unsubmitted slot must count as in flight");
    strategy.shutdown();
    check(!strategy.hasInFlightWork(),
          "shutdown must reclaim slots stranded in PendingQueueAccess");
    check(submitCount == submitsBefore,
          "shutdown must not submit a reclaimed pending command");
    strategy.shutdown();
    check(destroyFenceCount == fencesBefore + 3 &&
          destroyPoolCount == poolsBefore + 1,
          "reclaiming the stranded slot must allow full resource destruction");
}

} // namespace

int main()
{
    testSubmissionLifecycle();
    testThreeSlotNonblockingBackPressure();
    testFencePollingAndDeferredShutdown();
    testQueueSubmissionFailure();
    testDeviceShutdownAbandonsUnfinishedObjects();
    testPendingQueueAccessSlotIsReclaimedAtShutdown();
    if (failures)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
