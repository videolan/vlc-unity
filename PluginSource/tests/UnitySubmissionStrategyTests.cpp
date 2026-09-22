#include "../vulkan/UnitySubmissionStrategy.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

int failures = 0;
int createdEvents = 0;
int destroyedEvents = 0;
int resetEvents = 0;
int setCommands = 0;
VkEvent signaledEvent = VK_NULL_HANDLE;

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

VKAPI_ATTR VkResult VKAPI_CALL fakeCreateEvent(
    VkDevice, const VkEventCreateInfo*, const VkAllocationCallbacks*,
    VkEvent* event)
{
    *event = handle<VkEvent>(100 + createdEvents++);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fakeDestroyEvent(
    VkDevice, VkEvent, const VkAllocationCallbacks*)
{
    ++destroyedEvents;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeResetEvent(VkDevice, VkEvent event)
{
    ++resetEvents;
    if (signaledEvent == event)
        signaledEvent = VK_NULL_HANDLE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fakeGetEventStatus(VkDevice, VkEvent event)
{
    return event == signaledEvent ? VK_EVENT_SET : VK_EVENT_RESET;
}

VKAPI_ATTR void VKAPI_CALL fakeCmdSetEvent(
    VkCommandBuffer, VkEvent event, VkPipelineStageFlags stage)
{
    check(stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          "completion event must follow the copy and ownership release");
    signaledEvent = event;
    ++setCommands;
}

bool UNITY_INTERFACE_API fakeCommandRecordingState(
    UnityVulkanRecordingState* state, UnityVulkanGraphicsQueueAccess access)
{
    check(access == kUnityVulkanGraphicsQueueAccess_DontCare,
          "Unity-owned recording must not request direct queue access");
    memset(state, 0, sizeof(*state));
    state->commandBuffer = handle<VkCommandBuffer>(7);
    return true;
}

UnitySubmissionVulkanFunctions fakeFunctions()
{
    UnitySubmissionVulkanFunctions functions;
    functions.createEvent = fakeCreateEvent;
    functions.destroyEvent = fakeDestroyEvent;
    functions.resetEvent = fakeResetEvent;
    functions.getEventStatus = fakeGetEventStatus;
    functions.cmdSetEvent = fakeCmdSetEvent;
    return functions;
}

void testGpuObservableCompletion()
{
    IUnityGraphicsVulkan graphics = {};
    graphics.CommandRecordingState = fakeCommandRecordingState;
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    UnitySubmissionVulkanFunctions functions = fakeFunctions();

    const int createdBefore = createdEvents;
    const int destroyedBefore = destroyedEvents;
    UnitySubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "Unity submission strategy must preallocate completion events");
    check(createdEvents == createdBefore + 3,
          "exactly three completion events must be preallocated");
    check(!strategy.completesImmediately(),
          "recording into Unity's command buffer is not GPU completion");

    VulkanExternalFrame frame;
    frame.token = 42;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(strategy.begin(frame, commandBuffer) && commandBuffer != VK_NULL_HANDLE,
          "free completion slot must begin recording");

    // Keep the fake event unsignaled until after endAndSubmit to model Unity
    // recording the command before its queue submission executes.
    signaledEvent = VK_NULL_HANDLE;
    check(strategy.endAndSubmit(frame),
          "recorded copy must append its GPU completion event");
    const VkEvent submittedEvent = signaledEvent;
    signaledEvent = VK_NULL_HANDLE;
    VulkanSubmissionCompletion completion;
    check(strategy.collectCompletions(&completion, 1) == 0,
          "unsignaled GPU event must keep the source in flight");
    signaledEvent = submittedEvent;
    check(strategy.collectCompletions(&completion, 1) == 1 &&
              completion.token == 42 &&
              completion.status == VulkanSubmissionCompletionStatus::Complete,
          "signaled GPU event must publish the original token");
    check(!strategy.hasInFlightWork(),
          "completed event must recycle its fixed slot");
    strategy.shutdown();
    check(destroyedEvents == destroyedBefore + 3,
          "shutdown must destroy all preallocated completion events");
}

void testFixedRingBackPressureAndCancellation()
{
    IUnityGraphicsVulkan graphics = {};
    graphics.CommandRecordingState = fakeCommandRecordingState;
    UnityVulkanInstance instance = {};
    instance.device = handle<VkDevice>(1);
    UnitySubmissionVulkanFunctions functions = fakeFunctions();
    UnitySubmissionStrategy strategy;
    check(strategy.initialize(&graphics, instance, &functions),
          "fixed-ring fixture must initialize");
    const int createdAfterInitialization = createdEvents;

    for (uint64_t token = 1; token <= 3; ++token) {
        VulkanExternalFrame frame;
        frame.token = token;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        check(strategy.begin(frame, commandBuffer),
              "each fixed completion slot must be available once");
    }
    VulkanExternalFrame saturated;
    saturated.token = 4;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(!strategy.begin(saturated, commandBuffer),
          "full completion ring must defer without allocating or waiting");
    check(createdEvents == createdAfterInitialization,
          "steady-state recording must not allocate Vulkan events");

    for (uint64_t token = 1; token <= 3; ++token) {
        VulkanExternalFrame frame;
        frame.token = token;
        strategy.cancel(frame);
    }
    check(!strategy.hasInFlightWork(),
          "cancelling recordings must recycle the ring");
    strategy.shutdown();
}

} // namespace

int main()
{
    testGpuObservableCompletion();
    testFixedRingBackPressureAndCancellation();
    if (failures)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
