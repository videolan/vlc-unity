#pragma once

#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#include "vulkan/VulkanFrameSource.h"
#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityGraphicsVulkan.h"

#include <android/hardware_buffer.h>
#include <array>
#include <mutex>

class AndroidVulkanFrameSource final : public IVulkanFrameSource
{
public:
    static constexpr size_t SlotCount = 3;

    bool initialize(const UnityVulkanInstance& instance);
    void shutdown();
    bool importHardwareBuffer(size_t index, AHardwareBuffer* buffer,
                              unsigned width, unsigned height);
    void releaseSlot(size_t index);
    // Releases every slot with a single device-wide drain instead of one
    // vkDeviceWaitIdle per slot.
    void releaseAllSlots();
    void publish(size_t index);
    bool availableForProducer(size_t index) const;
    void discardReadyFrames();

    VkImage image(size_t index) const;

    bool acquireReadyFrame(VulkanExternalFrame& frame) override;
    void onCopyDeferred(uint64_t token) override { (void)token; }
    bool onCopyScheduled(uint64_t token) override;
    void onCopyComplete(uint64_t token) override;
    void onCopyFailed(uint64_t token) override;

private:
    struct Slot
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkExtent3D extent = { 0, 0, 0 };
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool ready = false;
        bool inFlight = false;
    };

    void releaseAllSlotsLocked();
    void destroySlot(Slot& slot);

    UnityVulkanInstance m_instance = {};
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID
        m_getHardwareBufferProperties = nullptr;
    std::array<Slot, SlotCount> m_slots;
    size_t m_readyIndex = 0;
    bool m_hasReady = false;
    mutable std::mutex m_mutex;
};
