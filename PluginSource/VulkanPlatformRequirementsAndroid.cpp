#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#include "VulkanPlatformRequirements.h"

VulkanPlatformRequirements GetVulkanPlatformRequirements()
{
    VulkanPlatformRequirements requirements;
    requirements.platformName = "Android";
    requirements.instanceExtensions = {
        { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
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
        { VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, VK_API_VERSION_1_1 },
        { VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_API_VERSION_1_1 },
        { "VK_ANDROID_external_memory_android_hardware_buffer", 0 },
    };
    return requirements;
}
