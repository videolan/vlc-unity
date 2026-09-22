#pragma once

#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityGraphicsVulkan.h"
#include "VulkanSubmissionStrategy.h"

#include <array>
#include <atomic>

struct OwnedQueueVulkanFunctions
{
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkGetFenceStatus getFenceStatus = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
};

class OwnedQueueSubmissionStrategy final : public IVulkanSubmissionStrategy
{
public:
    static constexpr size_t SlotCount = 3;

    OwnedQueueSubmissionStrategy() = default;
    ~OwnedQueueSubmissionStrategy() override;

    bool initialize(IUnityGraphicsVulkan* graphics,
                    const UnityVulkanInstance& instance,
                    const OwnedQueueVulkanFunctions* functions = nullptr);

    bool begin(const VulkanExternalFrame& frame,
               VkCommandBuffer& commandBuffer) override;
    bool endAndSubmit(const VulkanExternalFrame& frame) override;
    void cancel(const VulkanExternalFrame& frame) override;
    size_t collectCompletions(VulkanSubmissionCompletion* completions,
                              size_t capacity) override;
    bool completesImmediately() const override { return false; }
    bool hasInFlightWork() const override;
    void shutdown() override;
    void submitPendingQueueWork();
    void abandonForDeviceShutdown();

private:
    enum class State
    {
        Free,
        Recording,
        PendingQueueAccess,
        Submitted,
        Failed,
    };

    struct Slot
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore waitSemaphore = VK_NULL_HANDLE;
        VkSemaphore signalSemaphore = VK_NULL_HANDLE;
        uint64_t token = 0;
        std::atomic<State> state { State::Free };
    };

    void submit(Slot& slot);
    void destroyResources();
    Slot* findRecordingSlot(uint64_t token);

    UnityVulkanInstance m_instance = {};
    OwnedQueueVulkanFunctions m_functions = {};
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<Slot, SlotCount> m_slots;
    bool m_initialized = false;
};
