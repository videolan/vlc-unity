#include "UnitySubmissionStrategy.h"

UnitySubmissionStrategy::~UnitySubmissionStrategy()
{
    shutdown();
}

bool UnitySubmissionStrategy::initialize(
    IUnityGraphicsVulkan* graphics, const UnityVulkanInstance& instance,
    const UnitySubmissionVulkanFunctions* supplied)
{
    if (!graphics || instance.device == VK_NULL_HANDLE)
        return false;

    UnitySubmissionVulkanFunctions functions = {};
    if (supplied) {
        functions = *supplied;
    } else {
        if (!instance.getInstanceProcAddr)
            return false;
        const auto getDeviceProcAddr =
            reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                instance.getInstanceProcAddr(
                    instance.instance, "vkGetDeviceProcAddr"));
        if (!getDeviceProcAddr)
            return false;
        functions.createEvent = reinterpret_cast<PFN_vkCreateEvent>(
            getDeviceProcAddr(instance.device, "vkCreateEvent"));
        functions.destroyEvent = reinterpret_cast<PFN_vkDestroyEvent>(
            getDeviceProcAddr(instance.device, "vkDestroyEvent"));
        functions.resetEvent = reinterpret_cast<PFN_vkResetEvent>(
            getDeviceProcAddr(instance.device, "vkResetEvent"));
        functions.getEventStatus = reinterpret_cast<PFN_vkGetEventStatus>(
            getDeviceProcAddr(instance.device, "vkGetEventStatus"));
        functions.cmdSetEvent = reinterpret_cast<PFN_vkCmdSetEvent>(
            getDeviceProcAddr(instance.device, "vkCmdSetEvent"));
    }
    if (!functions.createEvent || !functions.destroyEvent ||
        !functions.resetEvent || !functions.getEventStatus ||
        !functions.cmdSetEvent) {
        return false;
    }

    m_graphics = graphics;
    m_instance = instance;
    m_functions = functions;
    for (Slot& slot : m_slots) {
        VkEventCreateInfo eventInfo = {};
        eventInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        if (m_functions.createEvent(
                m_instance.device, &eventInfo, nullptr,
                &slot.completionEvent) != VK_SUCCESS) {
            destroyEvents();
            m_graphics = nullptr;
            m_instance = {};
            m_functions = {};
            return false;
        }
    }
    m_initialized = true;
    return true;
}

UnitySubmissionStrategy::Slot* UnitySubmissionStrategy::findSlot(
    uint64_t token, State state)
{
    for (Slot& slot : m_slots) {
        if (slot.token == token && slot.state == state)
            return &slot;
    }
    return nullptr;
}

bool UnitySubmissionStrategy::begin(const VulkanExternalFrame& frame,
                                    VkCommandBuffer& commandBuffer)
{
    commandBuffer = VK_NULL_HANDLE;
    if (!m_initialized || !m_graphics)
        return false;
    for (const Slot& slot : m_slots) {
        if (slot.state != State::Free && slot.token == frame.token)
            return false;
    }
    Slot* selected = nullptr;
    for (Slot& slot : m_slots) {
        if (slot.state == State::Free) {
            selected = &slot;
            break;
        }
    }
    if (!selected || m_functions.resetEvent(
            m_instance.device, selected->completionEvent) != VK_SUCCESS) {
        return false;
    }

    UnityVulkanRecordingState recording = {};
    if (!m_graphics->CommandRecordingState(
            &recording, kUnityVulkanGraphicsQueueAccess_DontCare) ||
        recording.commandBuffer == VK_NULL_HANDLE) {
        return false;
    }
    selected->token = frame.token;
    selected->commandBuffer = recording.commandBuffer;
    selected->state = State::Recording;
    commandBuffer = recording.commandBuffer;
    return true;
}

bool UnitySubmissionStrategy::endAndSubmit(const VulkanExternalFrame& frame)
{
    Slot* slot = findSlot(frame.token, State::Recording);
    if (!slot)
        return false;
    m_functions.cmdSetEvent(
        slot->commandBuffer, slot->completionEvent,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    // Unity owns and submits this command buffer. The event is signaled only
    // after the copy and its preceding barriers execute on the GPU.
    slot->state = State::Submitted;
    return true;
}

void UnitySubmissionStrategy::cancel(const VulkanExternalFrame& frame)
{
    Slot* slot = findSlot(frame.token, State::Recording);
    if (!slot)
        return;
    slot->commandBuffer = VK_NULL_HANDLE;
    slot->state = State::Free;
}

size_t UnitySubmissionStrategy::collectCompletions(
    VulkanSubmissionCompletion* completions, size_t capacity)
{
    if (!m_initialized || !completions || capacity == 0)
        return 0;
    size_t count = 0;
    for (Slot& slot : m_slots) {
        if (slot.state == State::Submitted) {
            const VkResult status = m_functions.getEventStatus(
                m_instance.device, slot.completionEvent);
            if (status == VK_EVENT_RESET)
                continue;
            if (status != VK_EVENT_SET)
                slot.state = State::Failed;
        }
        if ((slot.state == State::Submitted || slot.state == State::Failed) &&
            count < capacity) {
            completions[count].token = slot.token;
            completions[count].status = slot.state == State::Submitted
                ? VulkanSubmissionCompletionStatus::Complete
                : VulkanSubmissionCompletionStatus::Failed;
            ++count;
            slot.commandBuffer = VK_NULL_HANDLE;
            slot.state = State::Free;
        }
    }
    return count;
}

bool UnitySubmissionStrategy::hasInFlightWork() const
{
    for (const Slot& slot : m_slots) {
        if (slot.state != State::Free)
            return true;
    }
    return false;
}

void UnitySubmissionStrategy::destroyEvents()
{
    if (m_instance.device != VK_NULL_HANDLE && m_functions.destroyEvent) {
        for (Slot& slot : m_slots) {
            if (slot.completionEvent != VK_NULL_HANDLE) {
                m_functions.destroyEvent(
                    m_instance.device, slot.completionEvent, nullptr);
            }
            slot = {};
        }
    }
}

void UnitySubmissionStrategy::shutdown()
{
    if (!m_initialized)
        return;
    VulkanSubmissionCompletion ignored[SlotCount];
    (void)collectCompletions(ignored, SlotCount);
    if (hasInFlightWork())
        return;
    destroyEvents();
    m_graphics = nullptr;
    m_instance = {};
    m_functions = {};
    m_initialized = false;
}

void UnitySubmissionStrategy::abandonForDeviceShutdown()
{
    // A Unity-owned command buffer may have recorded an event but never have
    // reached queue submission when device/plugin teardown starts. Do not
    // destroy an object referenced by that command buffer; VkDevice teardown
    // reclaims it together with the command buffer.
    for (Slot& slot : m_slots)
        slot = {};
    m_graphics = nullptr;
    m_instance = {};
    m_functions = {};
    m_initialized = false;
}
