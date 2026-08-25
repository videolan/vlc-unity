#include "../VulkanPlatformRequirements.h"

#include <cstring>
#include <iostream>

namespace {

int failures = 0;
void check(bool condition, const char* message);
bool fakeHasRenderIdentity = true;
bool fakeSemaphoreSharing = true;

template <typename T>
T handle(uintptr_t value)
{
    return reinterpret_cast<T>(value);
}

VKAPI_ATTR void VKAPI_CALL fakeGetPhysicalDeviceProperties2(
    VkPhysicalDevice, VkPhysicalDeviceProperties2* properties)
{
    auto* drm = static_cast<VkPhysicalDeviceDrmPropertiesEXT*>(
        properties ? properties->pNext : nullptr);
    if (drm && drm->sType ==
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT) {
        drm->hasRender = fakeHasRenderIdentity ? VK_TRUE : VK_FALSE;
        drm->renderMajor = 226;
        drm->renderMinor = 128;
    }
}

VKAPI_ATTR void VKAPI_CALL fakeGetExternalSemaphoreProperties(
    VkPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* query,
    VkExternalSemaphoreProperties* properties)
{
    check(query && query->handleType ==
              VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
          "Linux capability gate must query opaque-fd semaphores");
    if (!properties)
        return;
    if (fakeSemaphoreSharing) {
        properties->externalSemaphoreFeatures =
            VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
        properties->compatibleHandleTypes =
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetInstanceProcAddr(
    VkInstance, const char* name)
{
    if (strcmp(name, "vkGetPhysicalDeviceProperties2") == 0 ||
        strcmp(name, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            fakeGetPhysicalDeviceProperties2);
    }
    if (strcmp(name, "vkGetPhysicalDeviceExternalSemaphoreProperties") == 0 ||
        strcmp(name, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            fakeGetExternalSemaphoreProperties);
    }
    return nullptr;
}

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testResolutionAndDeduplication()
{
    const std::vector<const char*> requested = { "A", "A", "B" };
    const std::vector<std::string> supported = { "A", "B", "C", "PROMOTED" };
    const std::vector<VulkanExtensionRequirement> required = {
        { "B", 0 }, { "C", 0 }, { "PROMOTED", VK_API_VERSION_1_1 }
    };

    VulkanRequirementResolution result = VulkanResolveExtensionRequirements(
        requested, supported, required, VK_API_VERSION_1_1);
    check(result.success, "supported requirements must resolve");
    check(result.enabledNames.size() == 3,
          "requested and injected names must be deduplicated");
    if (result.enabledNames.size() == 3) {
        check(strcmp(result.enabledNames[0], "A") == 0 &&
              strcmp(result.enabledNames[1], "B") == 0 &&
              strcmp(result.enabledNames[2], "C") == 0,
              "resolution must retain request order and append requirements");
    }

    result = VulkanResolveExtensionRequirements(
        {}, supported, required, VK_API_VERSION_1_0);
    check(result.success && result.enabledNames.size() == 3,
          "a non-promoted dependency must be enabled on Vulkan 1.0");

    const std::vector<VulkanExtensionRequirement> optional = {
        { "OPTIONAL", 0 }, { "MISSING_OPTIONAL", 0 }
    };
    const std::vector<std::string> optionalSupported = { "OPTIONAL" };
    result = VulkanResolveExtensionRequirements(
        {}, optionalSupported, {}, VK_API_VERSION_1_0, optional);
    check(result.success && result.enabledNames.size() == 1 &&
          strcmp(result.enabledNames[0], "OPTIONAL") == 0,
          "supported optional extensions must enable without making missing ones fatal");
}

void testOptionalLayerMerge()
{
    const std::vector<const char*> requested = {
        "existing-layer", "existing-layer"
    };
    const std::vector<std::string> supported = {
        "existing-layer", "VK_LAYER_KHRONOS_validation"
    };
    const std::vector<const char*> optional = {
        "VK_LAYER_KHRONOS_validation", "missing-layer"
    };
    const std::vector<const char*> result = VulkanMergeOptionalLayers(
        requested, supported, optional);
    check(result.size() == 2,
          "optional layers must be supported and deduplicated");
    if (result.size() == 2) {
        check(strcmp(result[0], "existing-layer") == 0 &&
              strcmp(result[1], "VK_LAYER_KHRONOS_validation") == 0,
              "optional validation must preserve request order and skip missing layers");
    }
}

void testLinuxProviderMetadataAndNegativeValidator()
{
    const VulkanPlatformRequirements requirements =
        GetVulkanPlatformRequirements();
    check(requirements.capabilityValidators.size() == 1,
          "Linux provider must expose its physical capability validator");
    if (!requirements.capabilityValidators.empty()) {
        std::string diagnostic;
        check(!requirements.capabilityValidators[0].validate(
                  VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, diagnostic) &&
              !diagnostic.empty(),
              "missing physical-device dispatch must fail with a diagnostic");

        fakeHasRenderIdentity = false;
        fakeSemaphoreSharing = true;
        check(!requirements.capabilityValidators[0].validate(
                  handle<VkPhysicalDevice>(1), handle<VkInstance>(2),
                  fakeGetInstanceProcAddr, diagnostic) &&
              diagnostic.find("render-node identity") != std::string::npos,
              "missing DRM render identity must fail at device bootstrap");

        fakeHasRenderIdentity = true;
        fakeSemaphoreSharing = false;
        check(!requirements.capabilityValidators[0].validate(
                  handle<VkPhysicalDevice>(1), handle<VkInstance>(2),
                  fakeGetInstanceProcAddr, diagnostic) &&
              diagnostic.find("opaque-fd") != std::string::npos,
              "missing bidirectional semaphore support must fail at bootstrap");

        fakeSemaphoreSharing = true;
        check(requirements.capabilityValidators[0].validate(
                  handle<VkPhysicalDevice>(1), handle<VkInstance>(2),
                  fakeGetInstanceProcAddr, diagnostic),
              "exact DRM identity and bidirectional semaphore support must pass");
    }
}

void testMissingRequirement()
{
    const std::vector<VulkanExtensionRequirement> required = {
        { "required-extension", 0 }
    };
    const VulkanRequirementResolution result = VulkanResolveExtensionRequirements(
        {}, {}, required, VK_API_VERSION_1_3);
    check(!result.success, "missing required extension must fail");
    check(result.missingName == "required-extension",
          "failure must name the exact missing requirement");
}

} // namespace

int main()
{
    testResolutionAndDeduplication();
    testOptionalLayerMerge();
    testMissingRequirement();
    testLinuxProviderMetadataAndNegativeValidator();
    if (failures)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
