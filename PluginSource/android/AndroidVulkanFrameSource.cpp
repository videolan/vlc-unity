#include "AndroidVulkanFrameSource.h"
#include "Log.h"
#include "vulkan/VulkanUnityCopyCore.h"

bool AndroidVulkanFrameSource::initialize(const UnityVulkanInstance& instance)
{
    m_instance = instance;
    m_getHardwareBufferProperties =
        reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
            instance.getInstanceProcAddr(
                instance.instance, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    if (!m_getHardwareBufferProperties) {
        DEBUG("[Vulkan-Android] vkGetAndroidHardwareBufferPropertiesANDROID is unavailable");
        return false;
    }
    return true;
}

void AndroidVulkanFrameSource::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    releaseAllSlotsLocked();
    m_getHardwareBufferProperties = nullptr;
    m_instance = {};
}

void AndroidVulkanFrameSource::releaseSlot(size_t index)
{
    if (index >= SlotCount)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    Slot& slot = m_slots[index];
    if (slot.image != VK_NULL_HANDLE || slot.memory != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_instance.device);
    destroySlot(slot);
    if (m_hasReady && m_readyIndex == index)
        m_hasReady = false;
}

void AndroidVulkanFrameSource::releaseAllSlots()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    releaseAllSlotsLocked();
}

void AndroidVulkanFrameSource::releaseAllSlotsLocked()
{
    bool any = false;
    for (Slot& slot : m_slots) {
        if (!any && (slot.image != VK_NULL_HANDLE ||
                     slot.memory != VK_NULL_HANDLE)) {
            // Resize/device teardown can race Unity-owned command submission.
            // Drain once before freeing any imported source image.
            vkDeviceWaitIdle(m_instance.device);
            any = true;
        }
        destroySlot(slot);
    }
    m_hasReady = false;
}

void AndroidVulkanFrameSource::destroySlot(Slot& slot)
{
    if (slot.image == VK_NULL_HANDLE && slot.memory == VK_NULL_HANDLE) {
        slot = {};
        return;
    }
    if (slot.image != VK_NULL_HANDLE)
        vkDestroyImage(m_instance.device, slot.image, nullptr);
    if (slot.memory != VK_NULL_HANDLE)
        vkFreeMemory(m_instance.device, slot.memory, nullptr);
    slot = {};
}

bool AndroidVulkanFrameSource::importHardwareBuffer(
    size_t index, AHardwareBuffer* buffer, unsigned width, unsigned height)
{
    if (index >= SlotCount || !buffer || !m_getHardwareBufferProperties ||
        m_instance.device == VK_NULL_HANDLE) {
        return false;
    }

    releaseSlot(index);
    std::lock_guard<std::mutex> lock(m_mutex);
    Slot& slot = m_slots[index];

    VkExternalMemoryImageCreateInfo external = {};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkImageCreateInfo imageInfo = {};
    VulkanInitExternalImageInfo(
        imageInfo, width, height, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        &external);
    if (vkCreateImage(m_instance.device, &imageInfo, nullptr, &slot.image) != VK_SUCCESS) {
        DEBUG("[Vulkan-Android] external VkImage creation failed");
        return false;
    }

    VkAndroidHardwareBufferPropertiesANDROID properties = {};
    properties.sType =
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    if (m_getHardwareBufferProperties(
            m_instance.device, buffer, &properties) != VK_SUCCESS) {
        DEBUG("[Vulkan-Android] AHardwareBuffer property query failed");
        vkDestroyImage(m_instance.device, slot.image, nullptr);
        slot = {};
        return false;
    }

    const uint32_t memoryTypeIndex =
        VulkanFirstMemoryTypeBit(properties.memoryTypeBits);
    if (memoryTypeIndex == UINT32_MAX) {
        DEBUG("[Vulkan-Android] AHardwareBuffer has no compatible memory type");
        vkDestroyImage(m_instance.device, slot.image, nullptr);
        slot = {};
        return false;
    }

    VkMemoryDedicatedAllocateInfo dedicated = {};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.image = slot.image;

    VkImportAndroidHardwareBufferInfoANDROID import = {};
    import.sType =
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import.pNext = &dedicated;
    import.buffer = buffer;

    VkMemoryAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &import;
    allocation.allocationSize = properties.allocationSize;
    allocation.memoryTypeIndex = memoryTypeIndex;
    if (vkAllocateMemory(m_instance.device, &allocation, nullptr,
                         &slot.memory) != VK_SUCCESS ||
        vkBindImageMemory(m_instance.device, slot.image, slot.memory, 0) !=
            VK_SUCCESS) {
        DEBUG("[Vulkan-Android] external memory import or image bind failed");
        if (slot.memory != VK_NULL_HANDLE)
            vkFreeMemory(m_instance.device, slot.memory, nullptr);
        vkDestroyImage(m_instance.device, slot.image, nullptr);
        slot = {};
        return false;
    }

    slot.extent = { width, height, 1 };
    // GLES owns a newly imported AHardwareBuffer before Vulkan first reads it.
    // External/foreign producers expose their contents in GENERAL layout.
    slot.layout = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

void AndroidVulkanFrameSource::publish(size_t index)
{
    if (index >= SlotCount)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_slots[index].image == VK_NULL_HANDLE || m_slots[index].inFlight)
        return;
    m_slots[index].ready = true;
    m_readyIndex = index;
    m_hasReady = true;
}

bool AndroidVulkanFrameSource::availableForProducer(size_t index) const
{
    if (index >= SlotCount)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const Slot& slot = m_slots[index];
    return slot.image != VK_NULL_HANDLE && !slot.ready && !slot.inFlight;
}

void AndroidVulkanFrameSource::discardReadyFrames()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (Slot& slot : m_slots)
        slot.ready = false;
    m_hasReady = false;
}

VkImage AndroidVulkanFrameSource::image(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return index < SlotCount ? m_slots[index].image : VK_NULL_HANDLE;
}

bool AndroidVulkanFrameSource::acquireReadyFrame(VulkanExternalFrame& frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasReady)
        return false;
    const Slot& slot = m_slots[m_readyIndex];
    if (!slot.ready || slot.image == VK_NULL_HANDLE)
        return false;

    frame.image = slot.image;
    frame.format = kVulkanFrameFormat;
    frame.extent = slot.extent;
    frame.layout = slot.layout;
    frame.releaseLayout = VK_IMAGE_LAYOUT_GENERAL;
    frame.sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    frame.sourceAccess = VK_ACCESS_MEMORY_WRITE_BIT;
    frame.sourceQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
    frame.copyQueueFamily = m_instance.queueFamilyIndex;
    frame.releaseQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
    frame.acquireOwnership = true;
    frame.releaseOwnership = true;
    frame.token = m_readyIndex;
    return true;
}

bool AndroidVulkanFrameSource::onCopyScheduled(uint64_t token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (token >= SlotCount)
        return false;
    m_slots[token].ready = false;
    m_slots[token].inFlight = true;
    if (m_hasReady && m_readyIndex == token)
        m_hasReady = false;
    return true;
}

void AndroidVulkanFrameSource::onCopyComplete(uint64_t token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (token < SlotCount) {
        m_slots[token].layout = VK_IMAGE_LAYOUT_GENERAL;
        m_slots[token].inFlight = false;
    }
}

void AndroidVulkanFrameSource::onCopyFailed(uint64_t token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (token < SlotCount) {
        m_slots[token].ready = false;
        m_slots[token].inFlight = false;
        if (m_hasReady && m_readyIndex == token)
            m_hasReady = false;
    }
}
