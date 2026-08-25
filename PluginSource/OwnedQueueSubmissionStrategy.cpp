#include "OwnedQueueSubmissionStrategy.h"
#include "Log.h"

OwnedQueueSubmissionStrategy::~OwnedQueueSubmissionStrategy()
{
    shutdown();
}

bool OwnedQueueSubmissionStrategy::initialize(
    IUnityGraphicsVulkan* graphics, const UnityVulkanInstance& instance,
    const OwnedQueueVulkanFunctions* supplied)
{
    if (!graphics || instance.device == VK_NULL_HANDLE ||
        instance.graphicsQueue == VK_NULL_HANDLE)
        return false;
    m_instance = instance;
    if (supplied) {
        m_functions = *supplied;
    } else {
        if (!instance.getInstanceProcAddr)
            return false;
        const auto getDeviceProcAddr =
            reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                instance.getInstanceProcAddr(
                    instance.instance, "vkGetDeviceProcAddr"));
        if (!getDeviceProcAddr)
            return false;
        m_functions.createCommandPool =
            reinterpret_cast<PFN_vkCreateCommandPool>(
                getDeviceProcAddr(instance.device, "vkCreateCommandPool"));
        m_functions.destroyCommandPool =
            reinterpret_cast<PFN_vkDestroyCommandPool>(
                getDeviceProcAddr(instance.device, "vkDestroyCommandPool"));
        m_functions.allocateCommandBuffers =
            reinterpret_cast<PFN_vkAllocateCommandBuffers>(
                getDeviceProcAddr(instance.device, "vkAllocateCommandBuffers"));
        m_functions.resetCommandBuffer =
            reinterpret_cast<PFN_vkResetCommandBuffer>(
                getDeviceProcAddr(instance.device, "vkResetCommandBuffer"));
        m_functions.beginCommandBuffer =
            reinterpret_cast<PFN_vkBeginCommandBuffer>(
                getDeviceProcAddr(instance.device, "vkBeginCommandBuffer"));
        m_functions.endCommandBuffer =
            reinterpret_cast<PFN_vkEndCommandBuffer>(
                getDeviceProcAddr(instance.device, "vkEndCommandBuffer"));
        m_functions.createFence = reinterpret_cast<PFN_vkCreateFence>(
            getDeviceProcAddr(instance.device, "vkCreateFence"));
        m_functions.destroyFence = reinterpret_cast<PFN_vkDestroyFence>(
            getDeviceProcAddr(instance.device, "vkDestroyFence"));
        m_functions.resetFences = reinterpret_cast<PFN_vkResetFences>(
            getDeviceProcAddr(instance.device, "vkResetFences"));
        m_functions.getFenceStatus = reinterpret_cast<PFN_vkGetFenceStatus>(
            getDeviceProcAddr(instance.device, "vkGetFenceStatus"));
        m_functions.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(
            getDeviceProcAddr(instance.device, "vkQueueSubmit"));
    }
    if (!m_functions.createCommandPool || !m_functions.destroyCommandPool ||
        !m_functions.allocateCommandBuffers || !m_functions.resetCommandBuffer ||
        !m_functions.beginCommandBuffer || !m_functions.endCommandBuffer ||
        !m_functions.createFence || !m_functions.destroyFence ||
        !m_functions.resetFences || !m_functions.getFenceStatus ||
        !m_functions.queueSubmit) {
        return false;
    }

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = instance.queueFamilyIndex;
    if (m_functions.createCommandPool(
            instance.device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        return false;
    }
    VkCommandBuffer commandBuffers[SlotCount] = {};
    VkCommandBufferAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = m_commandPool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = SlotCount;
    if (m_functions.allocateCommandBuffers(
            instance.device, &allocation, commandBuffers) != VK_SUCCESS) {
        destroyResources();
        return false;
    }
    for (size_t i = 0; i < SlotCount; ++i) {
        m_slots[i].commandBuffer = commandBuffers[i];
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (m_functions.createFence(
                instance.device, &fenceInfo, nullptr,
                &m_slots[i].fence) != VK_SUCCESS) {
            destroyResources();
            return false;
        }
    }
    m_initialized = true;
    return true;
}

bool OwnedQueueSubmissionStrategy::begin(
    const VulkanExternalFrame& frame, VkCommandBuffer& commandBuffer)
{
    commandBuffer = VK_NULL_HANDLE;
    if (!m_initialized ||
        frame.sourceReady == VK_NULL_HANDLE ||
        frame.copyComplete == VK_NULL_HANDLE) {
        return false;
    }
    for (const Slot& candidate : m_slots) {
        if (candidate.state.load() != State::Free &&
            candidate.token == frame.token) {
            return false;
        }
    }
    Slot* selected = nullptr;
    for (Slot& candidate : m_slots) {
        State expected = State::Free;
        if (candidate.state.compare_exchange_strong(
                expected, State::Recording)) {
            selected = &candidate;
            break;
        }
    }
    if (!selected)
        return false;
    Slot& slot = *selected;
    slot.token = frame.token;
    if (m_functions.resetFences(
            m_instance.device, 1, &slot.fence) != VK_SUCCESS ||
        m_functions.resetCommandBuffer(slot.commandBuffer, 0) != VK_SUCCESS) {
        slot.state.store(State::Failed);
        return false;
    }
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (m_functions.beginCommandBuffer(
            slot.commandBuffer, &beginInfo) != VK_SUCCESS) {
        slot.state.store(State::Failed);
        return false;
    }
    slot.waitSemaphore = frame.sourceReady;
    slot.signalSemaphore = frame.copyComplete;
    commandBuffer = slot.commandBuffer;
    return true;
}

OwnedQueueSubmissionStrategy::Slot*
OwnedQueueSubmissionStrategy::findRecordingSlot(uint64_t token)
{
    for (Slot& slot : m_slots) {
        if (slot.state.load() == State::Recording && slot.token == token)
            return &slot;
    }
    return nullptr;
}

bool OwnedQueueSubmissionStrategy::endAndSubmit(
    const VulkanExternalFrame& frame)
{
    Slot* slot = findRecordingSlot(frame.token);
    if (!slot || m_functions.endCommandBuffer(
                     slot->commandBuffer) != VK_SUCCESS) {
        if (slot)
            slot->state.store(State::Failed);
        return false;
    }
    slot->state.store(State::PendingQueueAccess);
    // A second plugin event is configured with queue access and a Unity command
    // buffer flush. That synchronous event calls submitPendingQueueWork().
    return true;
}

void OwnedQueueSubmissionStrategy::cancel(const VulkanExternalFrame& frame)
{
    for (Slot& slot : m_slots) {
        const State state = slot.state.load();
        if (slot.token != frame.token ||
            (state != State::Recording && state != State::Failed)) {
            continue;
        }
        if (m_functions.resetCommandBuffer(slot.commandBuffer, 0) != VK_SUCCESS) {
            slot.state.store(State::Failed);
            return;
        }
        slot.waitSemaphore = VK_NULL_HANDLE;
        slot.signalSemaphore = VK_NULL_HANDLE;
        slot.state.store(State::Free);
        return;
    }
}

void OwnedQueueSubmissionStrategy::submitPendingQueueWork()
{
    if (!m_initialized)
        return;
    for (Slot& slot : m_slots) {
        if (slot.state.load() == State::PendingQueueAccess)
            submit(slot);
    }
}

void OwnedQueueSubmissionStrategy::submit(Slot& slot)
{
    if (slot.state.load() != State::PendingQueueAccess)
        return;
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &slot.waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &slot.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &slot.signalSemaphore;
    if (m_functions.queueSubmit(
            m_instance.graphicsQueue, 1, &submitInfo, slot.fence) != VK_SUCCESS) {
        slot.state.store(State::Failed);
        return;
    }
    slot.state.store(State::Submitted);
}

size_t OwnedQueueSubmissionStrategy::collectCompletions(
    VulkanSubmissionCompletion* completions, size_t capacity)
{
    if (!completions || capacity == 0)
        return 0;
    size_t count = 0;
    for (Slot& slot : m_slots) {
        State current = slot.state.load();
        if (current == State::Submitted) {
            const VkResult status = m_functions.getFenceStatus(
                m_instance.device, slot.fence);
            if (status == VK_NOT_READY)
                continue;
            if (status != VK_SUCCESS)
                slot.state.store(State::Failed);
        }
        if ((current == State::Submitted ||
             slot.state.load() == State::Failed) && count < capacity) {
            const bool success = slot.state.load() == State::Submitted;
            completions[count].token = slot.token;
            completions[count].status = success
                ? VulkanSubmissionCompletionStatus::Complete
                : VulkanSubmissionCompletionStatus::Failed;
            ++count;
            slot.waitSemaphore = VK_NULL_HANDLE;
            slot.signalSemaphore = VK_NULL_HANDLE;
            slot.state.store(State::Free);
        }
    }
    return count;
}

bool OwnedQueueSubmissionStrategy::hasInFlightWork() const
{
    for (const Slot& slot : m_slots) {
        if (slot.state.load() != State::Free)
            return true;
    }
    return false;
}

void OwnedQueueSubmissionStrategy::destroyResources()
{
    if (m_instance.device != VK_NULL_HANDLE) {
        for (Slot& slot : m_slots) {
            if (slot.fence != VK_NULL_HANDLE)
                m_functions.destroyFence(
                    m_instance.device, slot.fence, nullptr);
            slot.fence = VK_NULL_HANDLE;
            slot.commandBuffer = VK_NULL_HANDLE;
            slot.state.store(State::Free);
        }
        if (m_commandPool != VK_NULL_HANDLE)
            m_functions.destroyCommandPool(
                m_instance.device, m_commandPool, nullptr);
    }
    m_commandPool = VK_NULL_HANDLE;
}

void OwnedQueueSubmissionStrategy::shutdown()
{
    if (!m_initialized)
        return;
    VulkanSubmissionCompletion ignored[SlotCount];
    (void)collectCompletions(ignored, SlotCount);
    for (Slot& slot : m_slots) {
        if (slot.state.load() != State::PendingQueueAccess)
            continue;
        if (m_functions.resetCommandBuffer(slot.commandBuffer, 0) != VK_SUCCESS)
            continue;
        slot.waitSemaphore = VK_NULL_HANDLE;
        slot.signalSemaphore = VK_NULL_HANDLE;
        slot.state.store(State::Free);
    }
    if (hasInFlightWork()) {
        // Destruction is deferred by the renderer until nonblocking fence polls finish.
        return;
    }
    destroyResources();
    m_initialized = false;
    m_instance = {};
}

void OwnedQueueSubmissionStrategy::abandonForDeviceShutdown()
{
    // Unity is about to destroy VkDevice. An unexpectedly unfinished submit
    // cannot be waited here, and its device-owned objects cannot be destroyed
    // while in use. Forget them so VkDevice teardown reclaims them atomically.
    for (Slot& slot : m_slots) {
        slot.commandBuffer = VK_NULL_HANDLE;
        slot.fence = VK_NULL_HANDLE;
        slot.waitSemaphore = VK_NULL_HANDLE;
        slot.signalSemaphore = VK_NULL_HANDLE;
        slot.state.store(State::Free);
    }
    m_commandPool = VK_NULL_HANDLE;
    m_initialized = false;
    m_instance = {};
}
