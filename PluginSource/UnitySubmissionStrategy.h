#pragma once

#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityGraphicsVulkan.h"
#include "VulkanSubmissionStrategy.h"

#include <array>

struct UnitySubmissionVulkanFunctions
{
    PFN_vkCreateEvent createEvent = nullptr;
    PFN_vkDestroyEvent destroyEvent = nullptr;
    PFN_vkResetEvent resetEvent = nullptr;
    PFN_vkGetEventStatus getEventStatus = nullptr;
    PFN_vkCmdSetEvent cmdSetEvent = nullptr;
};

// Records copies into Unity's command buffer and appends a VkEvent signal.
// Polling that event distinguishes command recording from actual GPU
// completion without submitting to Unity's queue ourselves.
class UnitySubmissionStrategy final : public IVulkanSubmissionStrategy
{
public:
    static constexpr size_t SlotCount = 3;

    UnitySubmissionStrategy() = default;
    ~UnitySubmissionStrategy() override;

    bool initialize(IUnityGraphicsVulkan* graphics,
                    const UnityVulkanInstance& instance,
                    const UnitySubmissionVulkanFunctions* functions = nullptr);
    bool begin(const VulkanExternalFrame& frame,
               VkCommandBuffer& commandBuffer) override;
    bool endAndSubmit(const VulkanExternalFrame& frame) override;
    void cancel(const VulkanExternalFrame& frame) override;
    size_t collectCompletions(VulkanSubmissionCompletion* completions,
                              size_t capacity) override;
    bool completesImmediately() const override { return false; }
    bool hasInFlightWork() const override;
    void shutdown() override;
    void abandonForDeviceShutdown();

private:
    enum class State
    {
        Free,
        Recording,
        Submitted,
        Failed,
    };

    struct Slot
    {
        VkEvent completionEvent = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint64_t token = 0;
        State state = State::Free;
    };

    Slot* findSlot(uint64_t token, State state);
    void destroyEvents();

    IUnityGraphicsVulkan* m_graphics = nullptr;
    UnityVulkanInstance m_instance = {};
    UnitySubmissionVulkanFunctions m_functions = {};
    std::array<Slot, SlotCount> m_slots;
    bool m_initialized = false;
};
