#include "VulkanPlatformRequirements.h"

namespace {

bool validateLinuxExternalCapabilities(
    VkPhysicalDevice physicalDevice,
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    std::string& diagnostic)
{
    if (!physicalDevice || !instance || !getInstanceProcAddr) {
        diagnostic = "physical-device query dispatch is unavailable";
        return false;
    }
    auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));
    if (!properties2) {
        properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
    }
    auto semaphoreProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(
            getInstanceProcAddr(
                instance, "vkGetPhysicalDeviceExternalSemaphoreProperties"));
    if (!semaphoreProperties) {
        semaphoreProperties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(
                getInstanceProcAddr(
                    instance,
                    "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
    }
    if (!properties2 || !semaphoreProperties) {
        diagnostic = "required promoted physical-device query entry points are unavailable";
        return false;
    }

    VkPhysicalDeviceDrmPropertiesEXT drm = {};
    drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 properties = {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &drm;
    properties2(physicalDevice, &properties);
    if (drm.hasRender != VK_TRUE) {
        diagnostic = "VK_EXT_physical_device_drm reports no render-node identity";
        return false;
    }

    VkPhysicalDeviceExternalSemaphoreInfo query = {};
    query.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
    query.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkExternalSemaphoreProperties semaphore = {};
    semaphore.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
    semaphoreProperties(physicalDevice, &query, &semaphore);
    const VkExternalSemaphoreFeatureFlags required =
        VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
        VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
    if ((semaphore.externalSemaphoreFeatures & required) != required ||
        (semaphore.compatibleHandleTypes &
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) == 0) {
        diagnostic = "bidirectional opaque-fd semaphore sharing is unavailable";
        return false;
    }
    diagnostic.clear();
    return true;
}

} // namespace

VulkanPlatformRequirements GetVulkanPlatformRequirements()
{
    VulkanPlatformRequirements requirements;
    requirements.platformName = "Linux";
    requirements.instanceExtensions = {
        { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
          VK_API_VERSION_1_1 },
        { VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
          VK_API_VERSION_1_1 },
        { VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
          VK_API_VERSION_1_1 },
    };
#if VULKAN_ENABLE_VALIDATION
    requirements.optionalInstanceExtensions = {
        { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, 0 },
    };
    requirements.optionalInstanceLayers = {
        "VK_LAYER_KHRONOS_validation",
    };
#endif
    requirements.deviceExtensions = {
        { VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_API_VERSION_1_1 },
        { VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, VK_API_VERSION_1_1 },
        { VK_KHR_BIND_MEMORY_2_EXTENSION_NAME, VK_API_VERSION_1_1 },
        { VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_API_VERSION_1_1 },
        { VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, VK_API_VERSION_1_1 },
        { "VK_KHR_external_memory_fd", 0 },
        { "VK_EXT_external_memory_dma_buf", 0 },
        { "VK_EXT_image_drm_format_modifier", 0 },
        { "VK_EXT_queue_family_foreign", 0 },
        { "VK_KHR_external_semaphore_fd", 0 },
        { "VK_EXT_physical_device_drm", 0 },
    };
    requirements.capabilityValidators = {
        { "DRM render identity and bidirectional opaque-fd semaphores",
          validateLinuxExternalCapabilities },
    };
    return requirements;
}
