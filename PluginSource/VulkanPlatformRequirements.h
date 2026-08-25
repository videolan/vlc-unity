#pragma once

#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityGraphicsVulkan.h"

#include <cstdint>
#include <string>
#include <vector>

#ifndef VULKAN_ENABLE_VALIDATION
#define VULKAN_ENABLE_VALIDATION 0
#endif

struct VulkanExtensionRequirement
{
    const char* name = nullptr;
    uint32_t promotedInApiVersion = 0;
};

using VulkanCapabilityValidator = bool (*)(
    VkPhysicalDevice physicalDevice,
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    std::string& diagnostic);

struct VulkanCapabilityRequirement
{
    const char* name = nullptr;
    VulkanCapabilityValidator validate = nullptr;
};

struct VulkanPlatformRequirements
{
    const char* platformName = nullptr;
    std::vector<VulkanExtensionRequirement> instanceExtensions;
    std::vector<VulkanExtensionRequirement> deviceExtensions;
    std::vector<VulkanExtensionRequirement> optionalInstanceExtensions;
    std::vector<VulkanExtensionRequirement> optionalDeviceExtensions;
    std::vector<const char*> optionalInstanceLayers;
    std::vector<VulkanCapabilityRequirement> capabilityValidators;
};

struct VulkanRequirementResolution
{
    bool success = false;
    std::vector<const char*> enabledNames;
    std::string missingName;
};

VulkanRequirementResolution VulkanResolveExtensionRequirements(
    const std::vector<const char*>& requested,
    const std::vector<std::string>& supported,
    const std::vector<VulkanExtensionRequirement>& required,
    uint32_t apiVersion,
    const std::vector<VulkanExtensionRequirement>& optional = {});

std::vector<const char*> VulkanMergeOptionalLayers(
    const std::vector<const char*>& requested,
    const std::vector<std::string>& supported,
    const std::vector<const char*>& optional);

// Defined by the one platform provider selected by the build.
VulkanPlatformRequirements GetVulkanPlatformRequirements();

bool InitializeVulkanInterception(IUnityInterfaces* interfaces);
bool VulkanInterceptionWasRegistered();
bool VulkanInterceptionCreatedDevice();
const char* VulkanInterceptionFailure();

extern "C" const char* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_get_vulkan_interception_failure();
