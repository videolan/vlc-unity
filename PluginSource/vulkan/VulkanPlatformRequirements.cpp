#include "VulkanPlatformRequirements.h"
#include "Log.h"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace {

struct InterceptionState
{
    VulkanPlatformRequirements requirements;
    PFN_vkGetInstanceProcAddr originalGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance originalCreateInstance = nullptr;
    PFN_vkCreateDevice originalCreateDevice = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties enumerateInstanceExtensions = nullptr;
    PFN_vkEnumerateInstanceLayerProperties enumerateInstanceLayers = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensions = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    bool registered = false;
    bool callbackInvoked = false;
    bool deviceCreated = false;
    std::string failure;
    std::mutex mutex;
};

InterceptionState state;

bool containsName(const std::vector<const char*>& names, const char* name)
{
    return std::find_if(names.begin(), names.end(), [name](const char* item) {
        return item && name && strcmp(item, name) == 0;
    }) != names.end();
}

bool containsSupported(const std::vector<std::string>& names, const char* name)
{
    return std::find(names.begin(), names.end(), name ? name : "") != names.end();
}

std::string formatApiVersion(uint32_t version)
{
    return std::to_string(VK_VERSION_MAJOR(version)) + "." +
           std::to_string(VK_VERSION_MINOR(version)) + "." +
           std::to_string(VK_VERSION_PATCH(version));
}

bool enumerateInstanceExtensions(
    std::vector<std::string>& result, std::string& diagnostic)
{
    if (!state.enumerateInstanceExtensions) {
        diagnostic = "vkEnumerateInstanceExtensionProperties is unavailable";
        return false;
    }

    uint32_t count = 0;
    VkResult enumerationResult =
        state.enumerateInstanceExtensions(nullptr, &count, nullptr);
    if (enumerationResult != VK_SUCCESS) {
        diagnostic = "vkEnumerateInstanceExtensionProperties count query failed with VkResult=" +
            std::to_string(static_cast<int>(enumerationResult));
        return false;
    }
    std::vector<VkExtensionProperties> properties(count);
    enumerationResult = count
        ? state.enumerateInstanceExtensions(nullptr, &count, properties.data())
        : VK_SUCCESS;
    if (enumerationResult != VK_SUCCESS) {
        diagnostic = "vkEnumerateInstanceExtensionProperties data query failed with VkResult=" +
            std::to_string(static_cast<int>(enumerationResult));
        return false;
    }
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        result.emplace_back(properties[i].extensionName);
    diagnostic.clear();
    return true;
}

bool enumerateInstanceLayers(
    std::vector<std::string>& result, std::string& diagnostic)
{
    if (!state.enumerateInstanceLayers) {
        diagnostic = "vkEnumerateInstanceLayerProperties is unavailable";
        return false;
    }

    uint32_t count = 0;
    VkResult enumerationResult = state.enumerateInstanceLayers(&count, nullptr);
    if (enumerationResult != VK_SUCCESS) {
        diagnostic = "vkEnumerateInstanceLayerProperties count query failed with VkResult=" +
            std::to_string(static_cast<int>(enumerationResult));
        return false;
    }
    std::vector<VkLayerProperties> properties(count);
    enumerationResult = count
        ? state.enumerateInstanceLayers(&count, properties.data())
        : VK_SUCCESS;
    if (enumerationResult != VK_SUCCESS) {
        diagnostic = "vkEnumerateInstanceLayerProperties data query failed with VkResult=" +
            std::to_string(static_cast<int>(enumerationResult));
        return false;
    }
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        result.emplace_back(properties[i].layerName);
    diagnostic.clear();
    return true;
}

bool enumerateDeviceExtensions(
    VkPhysicalDevice physicalDevice,
    std::vector<std::string>& result,
    std::string& diagnostic)
{
    if (!state.enumerateDeviceExtensions) {
        diagnostic = "vkEnumerateDeviceExtensionProperties is unavailable";
        return false;
    }

    uint32_t count = 0;
    VkResult enumerationResult = state.enumerateDeviceExtensions(
        physicalDevice, nullptr, &count, nullptr);
    if (enumerationResult != VK_SUCCESS) {
        diagnostic = "vkEnumerateDeviceExtensionProperties count query failed with VkResult=" +
            std::to_string(static_cast<int>(enumerationResult));
        return false;
    }
    std::vector<VkExtensionProperties> properties(count);
    enumerationResult = count
        ? state.enumerateDeviceExtensions(
              physicalDevice, nullptr, &count, properties.data())
        : VK_SUCCESS;
    if (enumerationResult != VK_SUCCESS) {
        diagnostic = "vkEnumerateDeviceExtensionProperties data query failed with VkResult=" +
            std::to_string(static_cast<int>(enumerationResult));
        return false;
    }
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        result.emplace_back(properties[i].extensionName);
    diagnostic.clear();
    return true;
}

void setFailure(const std::string& message)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    state.failure = message;
    DEBUG("[Vulkan] %s", message.c_str());
}

VKAPI_ATTR VkResult VKAPI_CALL WrappedCreateInstance(
    const VkInstanceCreateInfo* createInfo,
    const VkAllocationCallbacks* allocator,
    VkInstance* instance)
{
    if (!createInfo) {
        setFailure("intercepted vkCreateInstance received a null create info");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!instance) {
        setFailure("intercepted vkCreateInstance received a null instance output");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!state.originalCreateInstance) {
        setFailure("intercepted vkCreateInstance has no loader dispatch");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const uint32_t apiVersion = createInfo->pApplicationInfo
        ? createInfo->pApplicationInfo->apiVersion : VK_API_VERSION_1_0;
    std::vector<const char*> requested;
    requested.reserve(createInfo->enabledExtensionCount);
    for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i)
        requested.push_back(createInfo->ppEnabledExtensionNames[i]);

    std::vector<std::string> supportedExtensions;
    std::string enumerationDiagnostic;
    if (!enumerateInstanceExtensions(
            supportedExtensions, enumerationDiagnostic)) {
        setFailure(enumerationDiagnostic);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VulkanRequirementResolution resolution = VulkanResolveExtensionRequirements(
        requested, supportedExtensions, state.requirements.instanceExtensions,
        apiVersion, state.requirements.optionalInstanceExtensions);
    if (!resolution.success) {
        setFailure(std::string("missing required Vulkan instance extension ") +
                   resolution.missingName + " for " +
                   state.requirements.platformName + " (Vulkan " +
                   formatApiVersion(apiVersion) + ")");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkInstanceCreateInfo modified = *createInfo;
    std::vector<const char*> requestedLayers;
    requestedLayers.reserve(createInfo->enabledLayerCount);
    for (uint32_t i = 0; i < createInfo->enabledLayerCount; ++i)
        requestedLayers.push_back(createInfo->ppEnabledLayerNames[i]);
    std::vector<std::string> supportedLayers;
    if (!enumerateInstanceLayers(supportedLayers, enumerationDiagnostic)) {
        DEBUG("[Vulkan] %s; continuing without optional instance layers",
              enumerationDiagnostic.c_str());
    }
    std::vector<const char*> enabledLayers = VulkanMergeOptionalLayers(
        requestedLayers, supportedLayers, state.requirements.optionalInstanceLayers);
    for (const char* layer : state.requirements.optionalInstanceLayers) {
        if (layer && !containsName(enabledLayers, layer))
            DEBUG("[Vulkan] optional instance layer %s is unavailable", layer);
    }
    modified.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    modified.ppEnabledLayerNames = enabledLayers.data();
    modified.enabledExtensionCount = static_cast<uint32_t>(resolution.enabledNames.size());
    modified.ppEnabledExtensionNames = resolution.enabledNames.data();
    const VkResult result = state.originalCreateInstance(
        &modified, allocator, instance);
    if (result != VK_SUCCESS) {
        setFailure("vkCreateInstance failed after Vulkan requirement injection with VkResult=" +
                   std::to_string(static_cast<int>(result)));
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL WrappedCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDevice* device)
{
    if (!physicalDevice) {
        setFailure("intercepted vkCreateDevice received a null physical device");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!createInfo) {
        setFailure("intercepted vkCreateDevice received a null create info");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!device) {
        setFailure("intercepted vkCreateDevice received a null device output");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!state.originalCreateDevice) {
        setFailure("intercepted vkCreateDevice has no loader dispatch");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t apiVersion = VK_API_VERSION_1_0;
    VkPhysicalDeviceProperties properties = {};
    if (state.getPhysicalDeviceProperties) {
        state.getPhysicalDeviceProperties(physicalDevice, &properties);
        apiVersion = properties.apiVersion;
    }

    std::vector<const char*> requested;
    requested.reserve(createInfo->enabledExtensionCount);
    for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i)
        requested.push_back(createInfo->ppEnabledExtensionNames[i]);

    std::vector<std::string> supportedExtensions;
    std::string enumerationDiagnostic;
    if (!enumerateDeviceExtensions(
            physicalDevice, supportedExtensions, enumerationDiagnostic)) {
        setFailure(enumerationDiagnostic);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VulkanRequirementResolution resolution = VulkanResolveExtensionRequirements(
        requested, supportedExtensions,
        state.requirements.deviceExtensions, apiVersion,
        state.requirements.optionalDeviceExtensions);
    if (!resolution.success) {
        std::string message = "missing required Vulkan device extension " +
            resolution.missingName + " for " + state.requirements.platformName;
        if (properties.deviceName[0] != '\0')
            message += std::string(" on ") + properties.deviceName;
        message += " (Vulkan " + formatApiVersion(apiVersion) + ")";
        setFailure(message);
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    for (const VulkanCapabilityRequirement& capability :
         state.requirements.capabilityValidators) {
        std::string diagnostic;
        if (!capability.validate || !capability.validate(
                physicalDevice, state.instance,
                state.originalGetInstanceProcAddr, diagnostic)) {
            std::string message = "missing required Vulkan capability ";
            message += capability.name ? capability.name : "unnamed-validator";
            if (!diagnostic.empty())
                message += ": " + diagnostic;
            if (properties.deviceName[0] != '\0')
                message += std::string(" on ") + properties.deviceName;
            message += " (Vulkan " + formatApiVersion(apiVersion) + ")";
            setFailure(message);
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
    }

    VkDeviceCreateInfo modified = *createInfo;
    modified.enabledExtensionCount = static_cast<uint32_t>(resolution.enabledNames.size());
    modified.ppEnabledExtensionNames = resolution.enabledNames.data();
    const VkResult result = state.originalCreateDevice(
        physicalDevice, &modified, allocator, device);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.deviceCreated = true;
    } else {
        setFailure("vkCreateDevice failed after Vulkan requirement injection with VkResult=" +
                   std::to_string(static_cast<int>(result)));
    }
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL WrappedGetInstanceProcAddr(
    VkInstance instance, const char* name)
{
    if (!name || !state.originalGetInstanceProcAddr)
        return nullptr;

    if (strcmp(name, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(WrappedCreateInstance);

    if (strcmp(name, "vkCreateDevice") == 0) {
        state.instance = instance;
        state.originalCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(
            state.originalGetInstanceProcAddr(instance, name));
        state.enumerateDeviceExtensions =
            reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
                state.originalGetInstanceProcAddr(
                    instance, "vkEnumerateDeviceExtensionProperties"));
        state.getPhysicalDeviceProperties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                state.originalGetInstanceProcAddr(
                    instance, "vkGetPhysicalDeviceProperties"));
        return reinterpret_cast<PFN_vkVoidFunction>(WrappedCreateDevice);
    }

    return state.originalGetInstanceProcAddr(instance, name);
}

PFN_vkGetInstanceProcAddr UNITY_INTERFACE_API InterceptionCallback(
    PFN_vkGetInstanceProcAddr getInstanceProcAddr, void*)
{
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.callbackInvoked = true;
    }
    if (!getInstanceProcAddr) {
        setFailure("Vulkan interception received a null loader dispatch");
        return nullptr;
    }
    state.originalGetInstanceProcAddr = getInstanceProcAddr;
    state.originalCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    state.enumerateInstanceExtensions =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            getInstanceProcAddr(VK_NULL_HANDLE,
                                "vkEnumerateInstanceExtensionProperties"));
    state.enumerateInstanceLayers =
        reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            getInstanceProcAddr(VK_NULL_HANDLE,
                                "vkEnumerateInstanceLayerProperties"));
    if (!state.originalCreateInstance || !state.enumerateInstanceExtensions ||
        !state.enumerateInstanceLayers) {
        setFailure("Vulkan interception could not resolve loader entry points");
        return getInstanceProcAddr;
    }
    DEBUG("[Vulkan] initialization interception active for %s",
          state.requirements.platformName);
    return WrappedGetInstanceProcAddr;
}

} // namespace

VulkanRequirementResolution VulkanResolveExtensionRequirements(
    const std::vector<const char*>& requested,
    const std::vector<std::string>& supported,
    const std::vector<VulkanExtensionRequirement>& required,
    uint32_t apiVersion,
    const std::vector<VulkanExtensionRequirement>& optional)
{
    VulkanRequirementResolution result;
    result.enabledNames.reserve(requested.size() + required.size());
    for (const char* name : requested) {
        if (name && !containsName(result.enabledNames, name))
            result.enabledNames.push_back(name);
    }

    for (const VulkanExtensionRequirement& requirement : required) {
        if (!requirement.name)
            continue;
        if (requirement.promotedInApiVersion != 0 &&
            apiVersion >= requirement.promotedInApiVersion) {
            continue;
        }
        if (!containsSupported(supported, requirement.name)) {
            result.missingName = requirement.name;
            return result;
        }
        if (!containsName(result.enabledNames, requirement.name))
            result.enabledNames.push_back(requirement.name);
    }
    for (const VulkanExtensionRequirement& requirement : optional) {
        if (!requirement.name ||
            (requirement.promotedInApiVersion != 0 &&
             apiVersion >= requirement.promotedInApiVersion) ||
            !containsSupported(supported, requirement.name)) {
            continue;
        }
        if (!containsName(result.enabledNames, requirement.name))
            result.enabledNames.push_back(requirement.name);
    }
    result.success = true;
    return result;
}

std::vector<const char*> VulkanMergeOptionalLayers(
    const std::vector<const char*>& requested,
    const std::vector<std::string>& supported,
    const std::vector<const char*>& optional)
{
    std::vector<const char*> result;
    result.reserve(requested.size() + optional.size());
    for (const char* name : requested) {
        if (name && !containsName(result, name))
            result.push_back(name);
    }
    for (const char* name : optional) {
        if (name && containsSupported(supported, name) &&
            !containsName(result, name)) {
            result.push_back(name);
        }
    }
    return result;
}

bool InitializeVulkanInterception(IUnityInterfaces* interfaces)
{
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.originalGetInstanceProcAddr = nullptr;
        state.originalCreateInstance = nullptr;
        state.originalCreateDevice = nullptr;
        state.enumerateInstanceExtensions = nullptr;
        state.enumerateInstanceLayers = nullptr;
        state.enumerateDeviceExtensions = nullptr;
        state.getPhysicalDeviceProperties = nullptr;
        state.instance = VK_NULL_HANDLE;
        state.registered = false;
        state.callbackInvoked = false;
        state.deviceCreated = false;
        state.failure.clear();
    }
    if (!interfaces) {
        setFailure("Vulkan initialization interception has no Unity interfaces");
        return false;
    }
    IUnityGraphicsVulkan* graphics = interfaces->Get<IUnityGraphicsVulkan>();
    if (!graphics) {
        setFailure("IUnityGraphicsVulkan is unavailable during plugin preload");
        return false;
    }

    state.requirements = GetVulkanPlatformRequirements();
    const bool registered = graphics->InterceptInitialization(
        InterceptionCallback, nullptr);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.registered = registered;
        if (!registered)
            state.failure = "Vulkan interception registration was too late; preload the native plugin before graphics initialization or select a non-Vulkan backend";
    }
    if (!registered)
        DEBUG("[Vulkan] %s", state.failure.c_str());
    return registered;
}

bool VulkanInterceptionWasRegistered()
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.registered;
}

bool VulkanInterceptionCreatedDevice()
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.deviceCreated;
}

const char* VulkanInterceptionFailure()
{
    static thread_local std::string snapshot;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.failure.empty())
        snapshot = state.failure;
    else if (!state.registered)
        snapshot = "Vulkan interception was not registered before device creation";
    else if (!state.callbackInvoked)
        snapshot = "Vulkan loader callback was not invoked after interception registration";
    else if (!state.deviceCreated)
        snapshot = "intercepted Vulkan device creation did not complete";
    else
        snapshot.clear();
    return snapshot.c_str();
}

extern "C" const char* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_get_vulkan_interception_failure()
{
    return VulkanInterceptionFailure();
}
