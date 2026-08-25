#include "LinuxDMABufFrameSource.h"
#include "LinuxDMABufWatermark.h"
#include "LinuxGraphicsInterop.h"
#include "Log.h"
#include "UniqueFd.h"
#include "VulkanPlatformRequirements.h"
#include "VulkanUnityCopyCore.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <sstream>
#include <unistd.h>
#include <vector>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif
#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#endif

LinuxDMABufFrameSource::LinuxDMABufFrameSource()
{
    for (auto& token : m_reservedTokens)
        token.store(0);
    for (auto& token : m_pendingCompletionTokens)
        token.store(0);
}

LinuxDMABufFrameSource::~LinuxDMABufFrameSource()
{
    shutdown();
}

bool LinuxDMABufFrameSource::initialize(
    IUnityGraphicsVulkan* graphics, const UnityVulkanInstance& instance)
{
    if (!graphics || instance.device == VK_NULL_HANDLE) {
        DEBUG("[Vulkan-Linux] Unity Vulkan device is unavailable. Select OpenGLCore.");
        return false;
    }
    if (!VulkanInterceptionWasRegistered() ||
        !VulkanInterceptionCreatedDevice()) {
        DEBUG("[Vulkan-Linux] required device extensions were not enabled before device creation (%s). Restart Unity after importing the preloaded plugin or select OpenGLCore.",
              VulkanInterceptionFailure());
        return false;
    }
    m_failed.store(false);
    m_forceDestroy = false;
    m_instance = instance;
    if (!loadVulkanFunctions() || !queryAndOpenExactDevice() ||
        !validateExternalSemaphoreSupport() || !createProducerContext()) {
        shutdown();
        return false;
    }
    m_producer.reset(new LinuxDMABufProducer(
        "Vulkan-Linux", m_gbm, *this, this, this, false));
    if (!m_producer->initialize() || !validateUuidCompatibility() ||
        !loadInteropFunctions()) {
        shutdown();
        return false;
    }
    m_initialized = true;
    DEBUG("[Vulkan-Linux] exact-device DMA-BUF producer initialized");
    return true;
}

bool LinuxDMABufFrameSource::loadVulkanFunctions()
{
    if (!m_instance.getInstanceProcAddr)
        return false;
    const auto loadInstance = [this](const char* coreName,
                                     const char* extensionName = nullptr) {
        PFN_vkVoidFunction function = m_instance.getInstanceProcAddr(
            m_instance.instance, coreName);
        if (!function && extensionName) {
            function = m_instance.getInstanceProcAddr(
                m_instance.instance, extensionName);
        }
        return function;
    };
    m_getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        loadInstance("vkGetDeviceProcAddr"));
    const auto loadDevice = [this](const char* name) {
        return m_getDeviceProcAddr
            ? m_getDeviceProcAddr(m_instance.device, name)
            : nullptr;
    };
    m_enumerateDeviceExtensionProperties =
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            loadInstance("vkEnumerateDeviceExtensionProperties"));
    m_getPhysicalDeviceProperties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            loadInstance("vkGetPhysicalDeviceProperties2",
                         "vkGetPhysicalDeviceProperties2KHR"));
    m_getPhysicalDeviceFormatProperties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(
            loadInstance("vkGetPhysicalDeviceFormatProperties2",
                         "vkGetPhysicalDeviceFormatProperties2KHR"));
    m_getPhysicalDeviceImageFormatProperties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
            loadInstance("vkGetPhysicalDeviceImageFormatProperties2",
                         "vkGetPhysicalDeviceImageFormatProperties2KHR"));
    m_getPhysicalDeviceExternalSemaphoreProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(
            loadInstance("vkGetPhysicalDeviceExternalSemaphoreProperties",
                         "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
    m_getPhysicalDeviceMemoryProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            loadInstance("vkGetPhysicalDeviceMemoryProperties"));
    m_createImage = reinterpret_cast<PFN_vkCreateImage>(
        loadDevice("vkCreateImage"));
    m_destroyImage = reinterpret_cast<PFN_vkDestroyImage>(
        loadDevice("vkDestroyImage"));
    m_getImageMemoryRequirements =
        reinterpret_cast<PFN_vkGetImageMemoryRequirements>(
            loadDevice("vkGetImageMemoryRequirements"));
    m_allocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(
        loadDevice("vkAllocateMemory"));
    m_freeMemory = reinterpret_cast<PFN_vkFreeMemory>(
        loadDevice("vkFreeMemory"));
    m_bindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(
        loadDevice("vkBindImageMemory"));
    m_createSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(
        loadDevice("vkCreateSemaphore"));
    m_destroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(
        loadDevice("vkDestroySemaphore"));
    const bool loaded = m_getDeviceProcAddr &&
        m_enumerateDeviceExtensionProperties &&
        m_getPhysicalDeviceProperties2 &&
        m_getPhysicalDeviceFormatProperties2 &&
        m_getPhysicalDeviceImageFormatProperties2 &&
        m_getPhysicalDeviceExternalSemaphoreProperties &&
        m_getPhysicalDeviceMemoryProperties && m_createImage &&
        m_destroyImage && m_getImageMemoryRequirements && m_allocateMemory &&
        m_freeMemory && m_bindImageMemory && m_createSemaphore &&
        m_destroySemaphore;
    if (!loaded) {
        DEBUG("[Vulkan-Linux] required Unity Vulkan entry points are unavailable");
    }
    return loaded;
}

bool LinuxDMABufFrameSource::queryAndOpenExactDevice()
{
    uint32_t extensionCount = 0;
    if (m_enumerateDeviceExtensionProperties(
            m_instance.physicalDevice, nullptr, &extensionCount, nullptr) !=
        VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (extensionCount && m_enumerateDeviceExtensionProperties(
            m_instance.physicalDevice, nullptr, &extensionCount,
            extensions.data()) != VK_SUCCESS) {
        return false;
    }
    const bool drmExtension = std::find_if(
        extensions.begin(), extensions.end(), [](const VkExtensionProperties& item) {
            return strcmp(item.extensionName, "VK_EXT_physical_device_drm") == 0;
        }) != extensions.end();

    VkPhysicalDeviceDrmPropertiesEXT drm = {};
    drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
    VkPhysicalDeviceIDProperties ids = {};
    ids.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    ids.pNext = &drm;
    VkPhysicalDeviceProperties2 properties = {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &ids;
    m_getPhysicalDeviceProperties2(m_instance.physicalDevice, &properties);

    LinuxVulkanDrmIdentity identity;
    identity.extensionAvailable = drmExtension;
    identity.hasRender = drm.hasRender == VK_TRUE;
    identity.renderMajor = static_cast<uint32_t>(drm.renderMajor);
    identity.renderMinor = static_cast<uint32_t>(drm.renderMinor);
    std::string diagnostic;
    if (!LinuxValidateVulkanDrmIdentity(identity, diagnostic)) {
        DEBUG("[Vulkan-Linux] %s on %s. Select OpenGLCore.",
              diagnostic.c_str(), properties.properties.deviceName);
        return false;
    }

    std::copy(ids.deviceUUID, ids.deviceUUID + VK_UUID_SIZE,
              m_vulkanUuids.device.begin());
    std::copy(ids.driverUUID, ids.driverUUID + VK_UUID_SIZE,
              m_vulkanUuids.driver.begin());
    const LinuxDrmMatchResult match = LinuxResolveVulkanDrmRenderNode(
        "/dev/dri", getenv("VLC_UNITY_DRM_DEVICE"), true,
        identity.renderMajor, identity.renderMinor);
    if (!match) {
        DEBUG("[Vulkan-Linux] exact DRM match failed (%s): %s. Device=%s reported=%u:%u. Select OpenGLCore.",
              LinuxDrmMatchErrorName(match.error), match.diagnostic.c_str(),
              properties.properties.deviceName, identity.renderMajor,
              identity.renderMinor);
        return false;
    }
    if (!m_gbm.open("Vulkan-Linux", match.path))
        return false;

    DEBUG("[Vulkan-Linux] device=%s DRM=%u:%u node=%s deviceUUID=%s driverUUID=%s",
          properties.properties.deviceName, identity.renderMajor,
          identity.renderMinor, match.path.c_str(),
          LinuxFormatUuid(m_vulkanUuids.device).c_str(),
          LinuxFormatUuid(m_vulkanUuids.driver).c_str());
    return true;
}

bool LinuxDMABufFrameSource::validateExternalSemaphoreSupport()
{
    VkPhysicalDeviceExternalSemaphoreInfo query = {};
    query.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
    query.handleType =
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkExternalSemaphoreProperties properties = {};
    properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
    m_getPhysicalDeviceExternalSemaphoreProperties(
        m_instance.physicalDevice, &query, &properties);

    std::string diagnostic;
    const bool supported = LinuxValidateOpaqueFdSemaphoreSupport(
        true,
        (properties.externalSemaphoreFeatures &
         VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) != 0,
        (properties.externalSemaphoreFeatures &
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) != 0,
        (properties.compatibleHandleTypes &
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) != 0,
        diagnostic);
    if (!supported) {
        DEBUG("[Vulkan-Linux] %s. Select OpenGLCore.", diagnostic.c_str());
    } else {
        DEBUG("[Vulkan-Linux] reusable opaque-fd semaphore import/export capability confirmed");
    }
    return supported;
}

bool LinuxDMABufFrameSource::createProducerContext()
{
    using GetPlatformDisplay = EGLDisplay (*)(EGLenum, void*, const EGLint*);
    auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplay>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay)
        m_display = getPlatformDisplay(EGL_PLATFORM_GBM_KHR, m_gbm.get(), nullptr);
    if (m_display == EGL_NO_DISPLAY)
        m_display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(m_gbm.get()));
    if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr) ||
        !eglBindAPI(EGL_OPENGL_API)) {
        DEBUG("[Vulkan-Linux] EGL/GBM producer display failed: 0x%x", eglGetError());
        return false;
    }
    const EGLint attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, 0,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(m_display, attributes, &config, 1, &count) || count == 0)
        return false;
    static const int versions[][2] = {{4, 5}, {3, 3}};
    for (const auto& version : versions) {
        const EGLint contextAttributes[] = {
            EGL_CONTEXT_MAJOR_VERSION, version[0],
            EGL_CONTEXT_MINOR_VERSION, version[1],
            EGL_CONTEXT_OPENGL_PROFILE_MASK,
            EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE
        };
        m_context = eglCreateContext(
            m_display, config, EGL_NO_CONTEXT, contextAttributes);
        if (m_context != EGL_NO_CONTEXT)
            break;
    }
    return m_context != EGL_NO_CONTEXT;
}

bool LinuxDMABufFrameSource::producerMakeCurrent(bool current)
{
    if (current) {
        if (m_display == EGL_NO_DISPLAY || m_context == EGL_NO_CONTEXT)
            return false;
        if (eglGetCurrentContext() == m_context)
            return true;
        return eglMakeCurrent(
            m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context) == EGL_TRUE;
    }
    if (eglGetCurrentContext() != m_context)
        return true;
    return eglMakeCurrent(
        m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE;
}

void* LinuxDMABufFrameSource::producerLoadProc(const char* name)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

bool LinuxDMABufFrameSource::onProducerSetup()
{
    return LinuxDMABufWatermarkSetup(
        *this,
#if defined(SHOW_WATERMARK)
        &m_watermark
#else
        nullptr
#endif
    );
}

void LinuxDMABufFrameSource::onProducerCleanup()
{
    LinuxDMABufWatermarkCleanup(
        *this,
#if defined(SHOW_WATERMARK)
        &m_watermark
#else
        nullptr
#endif
    );
}

bool LinuxDMABufFrameSource::onBeforeProducerSwap(
    GLuint framebuffer, unsigned width, unsigned height)
{
    return LinuxDMABufWatermarkBeforeSwap(
#if defined(SHOW_WATERMARK)
        &m_watermark,
#else
        nullptr,
#endif
        framebuffer, width, height);
}

bool LinuxDMABufFrameSource::validateUuidCompatibility()
{
    if (!producerMakeCurrent(true))
        return false;
    static const char* required[] = { "GL_EXT_memory_object" };
    const bool extension = LinuxGLHasExtensions(
        "Vulkan-Linux", [](const char* name, void* opaque) {
            return static_cast<LinuxDMABufFrameSource*>(opaque)->producerLoadProc(name);
        }, this, required, 1);
    using GetUnsignedByte = void (*)(GLenum, GLubyte*);
    auto getUnsignedByte = reinterpret_cast<GetUnsignedByte>(
        producerLoadProc("glGetUnsignedBytevEXT"));
    LinuxGraphicsDeviceUuids glUuids;
    bool querySucceeded = false;
    if (extension && getUnsignedByte) {
        clearGlErrors();
        getUnsignedByte(GL_DEVICE_UUID_EXT, glUuids.device.data());
        getUnsignedByte(GL_DRIVER_UUID_EXT, glUuids.driver.data());
        querySucceeded = glGetError() == GL_NO_ERROR;
    }
    std::string diagnostic;
    const bool compatible = LinuxValidateGraphicsDeviceUuids(
        extension && getUnsignedByte, querySucceeded,
        m_vulkanUuids, glUuids, diagnostic);
    if (!compatible) {
        DEBUG("[Vulkan-Linux] %s: GL deviceUUID=%s driverUUID=%s, Vulkan deviceUUID=%s driverUUID=%s. Select OpenGLCore.",
              diagnostic.c_str(), LinuxFormatUuid(glUuids.device).c_str(),
              LinuxFormatUuid(glUuids.driver).c_str(),
              LinuxFormatUuid(m_vulkanUuids.device).c_str(),
              LinuxFormatUuid(m_vulkanUuids.driver).c_str());
    } else {
        DEBUG("[Vulkan-Linux] exact GL/Vulkan UUID match: device=%s driver=%s",
              LinuxFormatUuid(glUuids.device).c_str(),
              LinuxFormatUuid(glUuids.driver).c_str());
    }
    producerMakeCurrent(false);
    return compatible;
}

bool LinuxDMABufFrameSource::loadInteropFunctions()
{
    if (!producerMakeCurrent(true))
        return false;
    static const char* required[] = {
        "GL_EXT_semaphore", "GL_EXT_semaphore_fd"
    };
    const bool extensions = LinuxGLHasExtensions(
        "Vulkan-Linux", [](const char* name, void* opaque) {
            return static_cast<LinuxDMABufFrameSource*>(opaque)->producerLoadProc(name);
        }, this, required, 2);
    m_glGenSemaphores = reinterpret_cast<PFNGLGENSEMAPHORESEXTPROC>(
        producerLoadProc("glGenSemaphoresEXT"));
    m_glDeleteSemaphores = reinterpret_cast<PFNGLDELETESEMAPHORESEXTPROC>(
        producerLoadProc("glDeleteSemaphoresEXT"));
    m_glImportSemaphoreFd = reinterpret_cast<PFNGLIMPORTSEMAPHOREFDEXTPROC>(
        producerLoadProc("glImportSemaphoreFdEXT"));
    m_glWaitSemaphore = reinterpret_cast<PFNGLWAITSEMAPHOREEXTPROC>(
        producerLoadProc("glWaitSemaphoreEXT"));
    m_glSignalSemaphore = reinterpret_cast<PFNGLSIGNALSEMAPHOREEXTPROC>(
        producerLoadProc("glSignalSemaphoreEXT"));
    producerMakeCurrent(false);

    if (m_getDeviceProcAddr) {
        m_getMemoryFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
            m_getDeviceProcAddr(m_instance.device, "vkGetMemoryFdPropertiesKHR"));
        m_getSemaphoreFd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
            m_getDeviceProcAddr(m_instance.device, "vkGetSemaphoreFdKHR"));
    }
    const bool loaded = extensions && m_glGenSemaphores &&
        m_glDeleteSemaphores && m_glImportSemaphoreFd && m_glWaitSemaphore &&
        m_glSignalSemaphore && m_getMemoryFdProperties && m_getSemaphoreFd;
    if (!loaded)
        DEBUG("[Vulkan-Linux] required reusable semaphore or fd functions are unavailable");
    return loaded;
}

uint32_t LinuxDMABufFrameSource::chooseMemoryType(uint32_t bits) const
{
    VkPhysicalDeviceMemoryProperties properties;
    m_getPhysicalDeviceMemoryProperties(m_instance.physicalDevice, &properties);
    const uint32_t index = VulkanFirstMemoryTypeBit(bits);
    return index < properties.memoryTypeCount ? index : UINT32_MAX;
}

bool LinuxDMABufFrameSource::importImage(
    size_t index, const LinuxDMABufSlot& source)
{
    const uint64_t minimumRowBytes = static_cast<uint64_t>(source.width) * 4;
    const uint64_t requiredEnd = source.height == 0
        ? UINT64_MAX
        : static_cast<uint64_t>(source.offset) +
          static_cast<uint64_t>(source.height - 1) * source.stride +
          minimumRowBytes;
    if (source.format != DRM_FORMAT_ABGR8888 ||
        source.modifier != DRM_FORMAT_MOD_LINEAR || source.planeCount != 1 ||
        source.width == 0 || source.height == 0 ||
        source.stride < minimumRowBytes || requiredEnd > source.size) {
        DEBUG("[Vulkan-Linux] invalid DMA-BUF metadata: slot=%zu format=0x%x modifier=0x%lx planes=%u extent=%ux%u offset=%u stride=%u size=%lu",
              index, source.format, static_cast<unsigned long>(source.modifier),
              source.planeCount, source.width, source.height, source.offset,
              source.stride, static_cast<unsigned long>(source.size));
        return false;
    }

    VkDrmFormatModifierPropertiesListEXT modifierList = {};
    modifierList.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
    VkFormatProperties2 formatProperties = {};
    formatProperties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    formatProperties.pNext = &modifierList;
    m_getPhysicalDeviceFormatProperties2(
        m_instance.physicalDevice, kVulkanFrameFormat, &formatProperties);
    std::vector<VkDrmFormatModifierPropertiesEXT> modifiers(
        modifierList.drmFormatModifierCount);
    modifierList.pDrmFormatModifierProperties = modifiers.data();
    m_getPhysicalDeviceFormatProperties2(
        m_instance.physicalDevice, kVulkanFrameFormat, &formatProperties);
    const bool linearSupported = std::find_if(
        modifiers.begin(), modifiers.end(), [](const VkDrmFormatModifierPropertiesEXT& item) {
            return item.drmFormatModifier == DRM_FORMAT_MOD_LINEAR &&
                   item.drmFormatModifierPlaneCount == 1 &&
                   (item.drmFormatModifierTilingFeatures &
                    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0;
        }) != modifiers.end();
    if (!linearSupported) {
        DEBUG("[Vulkan-Linux] explicit single-plane DRM_FORMAT_MOD_LINEAR is unsupported");
        return false;
    }

    VkPhysicalDeviceExternalImageFormatInfo externalQuery = {};
    externalQuery.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    externalQuery.handleType =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifierQuery = {};
    modifierQuery.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
    modifierQuery.pNext = &externalQuery;
    modifierQuery.drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
    modifierQuery.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkPhysicalDeviceImageFormatInfo2 imageQuery = {};
    imageQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    imageQuery.pNext = &modifierQuery;
    imageQuery.format = kVulkanFrameFormat;
    imageQuery.type = VK_IMAGE_TYPE_2D;
    imageQuery.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    imageQuery.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkExternalImageFormatProperties externalProperties = {};
    externalProperties.sType =
        VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 imageProperties = {};
    imageProperties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    imageProperties.pNext = &externalProperties;
    if (m_getPhysicalDeviceImageFormatProperties2(
            m_instance.physicalDevice, &imageQuery, &imageProperties) != VK_SUCCESS ||
        (externalProperties.externalMemoryProperties.externalMemoryFeatures &
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0 ||
        (externalProperties.externalMemoryProperties.compatibleHandleTypes &
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) == 0) {
        DEBUG("[Vulkan-Linux] external DMA-BUF image capability query failed");
        return false;
    }
    DEBUG("[Vulkan-Linux] external-image capability query passed: slot=%zu format=R8G8B8A8_UNORM modifier=DRM_FORMAT_MOD_LINEAR planes=1 handle=DMA_BUF",
          index);

    VkSubresourceLayout plane = {};
    plane.offset = source.offset;
    // VUID-VkImageDrmFormatModifierExplicitCreateInfoEXT-size-02267:
    // explicit DRM modifier plane layouts require size to be zero.
    plane.size = 0;
    plane.rowPitch = source.stride;
    VkImageDrmFormatModifierExplicitCreateInfoEXT explicitModifier = {};
    explicitModifier.sType =
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    explicitModifier.drmFormatModifier = source.modifier;
    explicitModifier.drmFormatModifierPlaneCount = 1;
    explicitModifier.pPlaneLayouts = &plane;
    VkExternalMemoryImageCreateInfo external = {};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external.pNext = &explicitModifier;
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkImageCreateInfo imageInfo = {};
    VulkanInitExternalImageInfo(
        imageInfo, source.width, source.height,
        VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &external);
    VulkanSlot& slot = m_slots[index];
    if (m_createImage(m_instance.device, &imageInfo, nullptr, &slot.image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memoryRequirements;
    m_getImageMemoryRequirements(
        m_instance.device, slot.image, &memoryRequirements);
    UniqueFd importFd(dup(source.fd));
    if (!importFd)
        return false;
    VkMemoryFdPropertiesKHR fdProperties = {};
    fdProperties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    if (m_getMemoryFdProperties(
            m_instance.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            importFd.get(), &fdProperties) != VK_SUCCESS) {
        return false;
    }
    const uint32_t memoryType = chooseMemoryType(
        memoryRequirements.memoryTypeBits & fdProperties.memoryTypeBits);
    if (memoryType == UINT32_MAX) {
        return false;
    }
    VkMemoryDedicatedAllocateInfo dedicated = {};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.image = slot.image;
    VkImportMemoryFdInfoKHR import = {};
    import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import.pNext = &dedicated;
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import.fd = importFd.get();
    VkMemoryAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &import;
    allocation.allocationSize = memoryRequirements.size;
    allocation.memoryTypeIndex = memoryType;
    const VkResult allocationResult = m_allocateMemory(
        m_instance.device, &allocation, nullptr, &slot.memory);
    if (allocationResult != VK_SUCCESS) {
        return false;
    }
    // A successful Vulkan import consumes the descriptor. On failure the
    // UniqueFd above retains ownership and closes it.
    (void)importFd.release();
    if (m_bindImageMemory(
            m_instance.device, slot.image, slot.memory, 0) != VK_SUCCESS) {
        m_freeMemory(m_instance.device, slot.memory, nullptr);
        slot.memory = VK_NULL_HANDLE;
        return false;
    }
    DEBUG("[Vulkan-Linux] DMA-BUF import passed: slot=%zu format=0x%x modifier=0x%lx planes=%u offset=%u stride=%u size=%lu memoryType=%u dedicated=1",
          index, source.format, static_cast<unsigned long>(source.modifier),
          source.planeCount, source.offset, source.stride,
          static_cast<unsigned long>(source.size), memoryType);
    return true;
}

bool LinuxDMABufFrameSource::createSharedSemaphore(
    VkSemaphore& semaphore, GLuint& glSemaphore)
{
    VkExportSemaphoreCreateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportInfo.handleTypes =
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &exportInfo;
    if (m_createSemaphore(
            m_instance.device, &createInfo, nullptr, &semaphore) != VK_SUCCESS)
        return false;
    VkSemaphoreGetFdInfoKHR getFd = {};
    getFd.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    getFd.semaphore = semaphore;
    getFd.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    int exportedFd = -1;
    if (m_getSemaphoreFd(
            m_instance.device, &getFd, &exportedFd) != VK_SUCCESS ||
        exportedFd < 0)
        return false;
    UniqueFd fd(exportedFd);
    m_glGenSemaphores(1, &glSemaphore);
    clearGlErrors();
    m_glImportSemaphoreFd(
        glSemaphore, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd.get());
    if (glGetError() != GL_NO_ERROR) {
        return false;
    }
    // Ownership transfers only after GL confirms a successful import.
    (void)fd.release();
    // No per-frame export/import occurs.
    return true;
}

bool LinuxDMABufFrameSource::createSemaphorePair(size_t index)
{
    VulkanSlot& slot = m_slots[index];
    return createSharedSemaphore(slot.sourceReady, slot.glSourceReady) &&
           createSharedSemaphore(slot.copyComplete, slot.glCopyComplete);
}

bool LinuxDMABufFrameSource::onDMABufSlotCreated(
    size_t index, const LinuxDMABufSlot& source)
{
    bool expected = false;
    if (!m_slotsChanging.compare_exchange_strong(expected, true))
        return false;
    bool result = false;
    if (index == 0) {
        std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
        if (lock.owns_lock()) {
            m_slotStates = {{
                SlotState::RenderingInGL,
                SlotState::AvailableToGL,
                SlotState::AvailableToGL,
            }};
            m_recycledSlots = {{ false, false, false }};
            m_readySequence = {{ 0, 0, 0 }};
            result = createSlotVulkanResources(index, source);
        }
    } else {
        result = createSlotVulkanResources(index, source);
    }
    if (!result) {
        DEBUG("[Vulkan-Linux] slot %zu DMA-BUF/semaphore import failed", index);
        destroyVulkanSlot(index, true);
    }
    m_slotsChanging.store(false);
    return result;
}

bool LinuxDMABufFrameSource::createSlotVulkanResources(
    size_t index, const LinuxDMABufSlot& source)
{
    const bool created = importImage(index, source) &&
                         createSemaphorePair(index);
    if (created) {
        m_slots[index].extent = { source.width, source.height, 1 };
        m_slots[index].token = m_nextToken.fetch_add(1) + 1;
    }
    return created;
}

void LinuxDMABufFrameSource::destroyGlSemaphores(VulkanSlot& slot)
{
    if (!m_glDeleteSemaphores)
        return;
    if (slot.glSourceReady) {
        m_glDeleteSemaphores(1, &slot.glSourceReady);
        slot.glSourceReady = 0;
    }
    if (slot.glCopyComplete) {
        m_glDeleteSemaphores(1, &slot.glCopyComplete);
        slot.glCopyComplete = 0;
    }
}

void LinuxDMABufFrameSource::destroyVulkanResources(
    VulkanSlot& slot, bool haveGlContext)
{
    if (haveGlContext)
        destroyGlSemaphores(slot);
    if (slot.sourceReady != VK_NULL_HANDLE)
        m_destroySemaphore(m_instance.device, slot.sourceReady, nullptr);
    if (slot.copyComplete != VK_NULL_HANDLE)
        m_destroySemaphore(m_instance.device, slot.copyComplete, nullptr);
    if (slot.image != VK_NULL_HANDLE)
        m_destroyImage(m_instance.device, slot.image, nullptr);
    if (slot.memory != VK_NULL_HANDLE)
        m_freeMemory(m_instance.device, slot.memory, nullptr);
    slot = {};
}

void LinuxDMABufFrameSource::destroyVulkanSlot(
    size_t index, bool haveGlContext)
{
    destroyVulkanResources(m_slots[index], haveGlContext);
}

bool LinuxDMABufFrameSource::beginDMABufSlotDestruction()
{
    bool expected = false;
    if (!m_slotsChanging.compare_exchange_strong(expected, true))
        return false;
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        m_slotsChanging.store(false);
        return false;
    }
    applyPendingStateLocked();
    return true;
}

void LinuxDMABufFrameSource::endDMABufSlotDestruction()
{
    m_slotsChanging.store(false);
}

void LinuxDMABufFrameSource::onDMABufSlotDestroying(
    size_t index, const LinuxDMABufSlot&)
{
    if (index >= m_slots.size())
        return;
    std::lock_guard<std::mutex> lock(m_stateMutex);
    VulkanSlot& slot = m_slots[index];
    const SlotState state = m_slotStates[index];
    const bool retain = !m_forceDestroy && slot.token != 0 &&
        (state == SlotState::ReadyForCopy ||
         state == SlotState::CopyInFlight);
    if (!retain) {
        destroyVulkanResources(slot, eglGetCurrentContext() == m_context);
        return;
    }

    destroyGlSemaphores(slot);
    RetiredSlot retired;
    retired.resources = slot;
    retired.ready = state == SlotState::ReadyForCopy;
    retired.inFlight = state == SlotState::CopyInFlight;
    retired.readySequence = m_readySequence[index];
    m_retiredSlots.push_back(retired);
    slot = {};
}

bool LinuxDMABufFrameSource::beginVulkanRendering(
    size_t index, const LinuxDMABufSlot& source)
{
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    applyPendingStateLocked();
    if (index >= m_slots.size() ||
        m_slotStates[index] != SlotState::RenderingInGL) {
        m_failed.store(true);
        return false;
    }
    if (!m_recycledSlots[index])
        return true;
    const GLenum layout = GL_LAYOUT_GENERAL_EXT;
    clearGlErrors();
    m_glWaitSemaphore(m_slots[index].glCopyComplete, 0, nullptr, 1,
                      &source.texture, &layout);
    if (glGetError() == GL_NO_ERROR)
        return true;
    m_slotStates[index] = SlotState::Failed;
    m_failed.store(true);
    return false;
}

bool LinuxDMABufFrameSource::advanceVulkanSlots(
    size_t rendered, const LinuxDMABufSlot& renderedSlot,
    size_t& next)
{
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    applyPendingStateLocked();
    if (rendered >= m_slots.size() ||
        m_slotStates[rendered] != SlotState::RenderingInGL) {
        m_failed.store(true);
        return false;
    }

    next = rendered;
    for (size_t offset = 1; offset < m_slots.size(); ++offset) {
        const size_t candidate = (rendered + offset) % m_slots.size();
        if (m_slotStates[candidate] == SlotState::AvailableToGL) {
            next = candidate;
            break;
        }
    }
    if (next == rendered)
        return false;

    const GLenum layout = GL_LAYOUT_GENERAL_EXT;
    clearGlErrors();
    m_glSignalSemaphore(m_slots[rendered].glSourceReady, 0, nullptr, 1,
                        &renderedSlot.texture, &layout);
    glFlush();
    if (glGetError() != GL_NO_ERROR) {
        m_slotStates[rendered] = SlotState::Failed;
        m_failed.store(true);
        return false;
    }
    m_slotStates[rendered] = SlotState::ReadyForCopy;
    m_slotStates[next] = SlotState::RenderingInGL;
    m_readySequence[rendered] = ++m_nextReadySequence;
    m_frameNotifications.fetch_add(1);
    m_pendingFrameCount.fetch_add(1);

    if (!m_recycledSlots[next])
        return true;
    const LinuxDMABufSlot& nextSlot = m_producer->slot(next);
    clearGlErrors();
    m_glWaitSemaphore(m_slots[next].glCopyComplete, 0, nullptr, 1,
                      &nextSlot.texture, &layout);
    if (glGetError() == GL_NO_ERROR)
        return true;
    m_slotStates[next] = SlotState::Failed;
    m_failed.store(true);
    return false;
}

bool LinuxDMABufFrameSource::acquireReadyFrame(VulkanExternalFrame& frame)
{
    if (m_slotsChanging.load()) {
        restoreFrameNotification();
        return false;
    }
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (!lock.owns_lock() || m_slotsChanging.load() || !m_producer) {
        restoreFrameNotification();
        return false;
    }
    applyPendingStateLocked();
    VulkanSlot* selected = nullptr;
    uint64_t oldest = UINT64_MAX;
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slotStates[i] != SlotState::ReadyForCopy)
            continue;
        if (m_readySequence[i] != 0 && m_readySequence[i] < oldest) {
            selected = &m_slots[i];
            oldest = m_readySequence[i];
        }
    }
    for (RetiredSlot& retired : m_retiredSlots) {
        if (retired.ready && retired.readySequence != 0 &&
            retired.readySequence < oldest) {
            selected = &retired.resources;
            oldest = retired.readySequence;
        }
    }
    if (selected && reserveToken(selected->token)) {
        frame.image = selected->image;
        frame.format = kVulkanFrameFormat;
        frame.extent = selected->extent;
        frame.layout = VK_IMAGE_LAYOUT_GENERAL;
        frame.releaseLayout = VK_IMAGE_LAYOUT_GENERAL;
        frame.sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        frame.sourceAccess = VK_ACCESS_MEMORY_WRITE_BIT;
        frame.sourceQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
        frame.copyQueueFamily = m_instance.queueFamilyIndex;
        frame.releaseQueueFamily = VK_QUEUE_FAMILY_FOREIGN_EXT;
        frame.sourceReady = selected->sourceReady;
        frame.copyComplete = selected->copyComplete;
        frame.token = selected->token;
        frame.acquireOwnership = true;
        frame.releaseOwnership = true;
        return true;
    }
    restoreFrameNotification();
    return false;
}

void LinuxDMABufFrameSource::restoreFrameNotification()
{
    if (m_pendingFrameCount.load() != 0)
        m_frameNotifications.fetch_add(1);
}

bool LinuxDMABufFrameSource::reserveToken(uint64_t token)
{
    if (token == 0)
        return false;
    for (const auto& reservation : m_reservedTokens) {
        if (reservation.load() == token)
            return false;
    }
    for (auto& reservation : m_reservedTokens) {
        uint64_t expected = 0;
        if (reservation.compare_exchange_strong(expected, token))
            return true;
    }
    return false;
}

bool LinuxDMABufFrameSource::releaseReservedToken(uint64_t token)
{
    for (auto& reservation : m_reservedTokens) {
        uint64_t expected = token;
        if (reservation.compare_exchange_strong(expected, 0))
            return true;
    }
    return false;
}

void LinuxDMABufFrameSource::onCopyDeferred(uint64_t token)
{
    if (releaseReservedToken(token))
        m_frameNotifications.fetch_add(1);
}

bool LinuxDMABufFrameSource::onCopyScheduled(uint64_t token)
{
    if (m_slotsChanging.load())
        return false;
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (!lock.owns_lock() || m_slotsChanging.load())
        return false;
    applyPendingStateLocked();
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].token != token)
            continue;
        if (m_slotStates[i] != SlotState::ReadyForCopy) {
            m_failed.store(true);
            return false;
        }
        m_slotStates[i] = SlotState::CopyInFlight;
        m_readySequence[i] = 0;
        releaseReservedToken(token);
        m_pendingFrameCount.fetch_sub(1);
        return true;
    }
    for (RetiredSlot& retired : m_retiredSlots) {
        if (retired.resources.token == token && retired.ready) {
            retired.ready = false;
            retired.inFlight = true;
            retired.readySequence = 0;
            releaseReservedToken(token);
            m_pendingFrameCount.fetch_sub(1);
            return true;
        }
    }
    m_failed.store(true);
    return false;
}

bool LinuxDMABufFrameSource::consumeFrameNotification()
{
    uint32_t count = m_frameNotifications.load();
    while (count != 0) {
        if (m_frameNotifications.compare_exchange_weak(count, count - 1))
            return true;
    }
    return false;
}

void LinuxDMABufFrameSource::discardPendingFrames()
{
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slotStates[i] != SlotState::ReadyForCopy)
            continue;
        releaseReservedToken(m_slots[i].token);
        m_slotStates[i] = SlotState::Failed;
    }
    for (auto retired = m_retiredSlots.begin();
         retired != m_retiredSlots.end();) {
        if (!retired->ready) {
            ++retired;
            continue;
        }
        releaseReservedToken(retired->resources.token);
        destroyVulkanResources(retired->resources, false);
        retired = m_retiredSlots.erase(retired);
    }
    m_pendingFrameCount.store(0);
    m_frameNotifications.store(0);
}

void LinuxDMABufFrameSource::onCopyComplete(uint64_t token)
{
    if (m_slotsChanging.load()) {
        if (!deferCompletion(token))
            m_failed.store(true);
        return;
    }
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        if (!deferCompletion(token))
            m_failed.store(true);
        return;
    }
    if (!completeTokenLocked(token))
        m_failed.store(true);
}

bool LinuxDMABufFrameSource::deferCompletion(uint64_t token)
{
    if (token == 0)
        return false;
    for (auto& pending : m_pendingCompletionTokens) {
        uint64_t expected = 0;
        if (pending.compare_exchange_strong(expected, token) ||
            expected == token) {
            return true;
        }
    }
    return false;
}

bool LinuxDMABufFrameSource::completeTokenLocked(uint64_t token)
{
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].token != token)
            continue;
        if (m_slotStates[i] != SlotState::CopyInFlight)
            return false;
        m_slotStates[i] = SlotState::AvailableToGL;
        m_recycledSlots[i] = true;
        return true;
    }
    for (auto retired = m_retiredSlots.begin();
         retired != m_retiredSlots.end(); ++retired) {
        if (retired->resources.token != token || !retired->inFlight)
            continue;
        destroyVulkanResources(retired->resources, false);
        m_retiredSlots.erase(retired);
        return true;
    }
    return false;
}

void LinuxDMABufFrameSource::applyPendingStateLocked()
{
    for (auto& pending : m_pendingCompletionTokens) {
        const uint64_t token = pending.exchange(0);
        if (token != 0 && !completeTokenLocked(token)) {
            m_failed.store(true);
        }
    }
}

void LinuxDMABufFrameSource::onCopyFailed(uint64_t token)
{
    releaseReservedToken(token);
    std::unique_lock<std::mutex> lock(m_stateMutex, std::try_to_lock);
    if (lock.owns_lock()) {
        for (size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].token != token)
                continue;
            if (m_slotStates[i] == SlotState::ReadyForCopy)
                m_pendingFrameCount.fetch_sub(1);
            if (m_slotStates[i] == SlotState::ReadyForCopy ||
                m_slotStates[i] == SlotState::CopyInFlight) {
                m_slotStates[i] = SlotState::Failed;
            }
            break;
        }
        for (auto retired = m_retiredSlots.begin();
             retired != m_retiredSlots.end(); ++retired) {
            if (retired->resources.token != token)
                continue;
            if (retired->ready)
                m_pendingFrameCount.fetch_sub(1);
            destroyVulkanResources(retired->resources, false);
            m_retiredSlots.erase(retired);
            break;
        }
    }
    m_failed.store(true);
    DEBUG("[Vulkan-Linux] copy submission failed for slot %lu; backend disabled",
          static_cast<unsigned long>(token));
}

bool LinuxDMABufFrameSource::setVlcContext(libvlc_media_player_t* mediaPlayer)
{
    return m_initialized && m_producer &&
        m_producer->setVlcContext(mediaPlayer);
}

void LinuxDMABufFrameSource::unsetVlcContext(libvlc_media_player_t* mediaPlayer)
{
    if (m_producer)
        m_producer->unsetVlcContext(mediaPlayer);
}

void LinuxDMABufFrameSource::shutdown()
{
    shutdownInternal(false);
}

void LinuxDMABufFrameSource::abandonForDeviceShutdown()
{
    shutdownInternal(true);
}

void LinuxDMABufFrameSource::shutdownInternal(bool abandonVulkanHandles)
{
    m_forceDestroy = true;
    if (abandonVulkanHandles) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        const auto abandon = [](VulkanSlot& slot) {
            slot.image = VK_NULL_HANDLE;
            slot.memory = VK_NULL_HANDLE;
            slot.sourceReady = VK_NULL_HANDLE;
            slot.copyComplete = VK_NULL_HANDLE;
        };
        for (VulkanSlot& slot : m_slots)
            abandon(slot);
        for (RetiredSlot& retired : m_retiredSlots)
            abandon(retired.resources);
    }
    if (m_producer) {
        m_producer->release();
        m_producer.reset();
    }
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (RetiredSlot& retired : m_retiredSlots)
            destroyVulkanResources(retired.resources, false);
        m_retiredSlots.clear();
    }
    if (m_context != EGL_NO_CONTEXT)
        eglDestroyContext(m_display, m_context);
    if (m_display != EGL_NO_DISPLAY)
        eglTerminate(m_display);
    m_context = EGL_NO_CONTEXT;
    m_display = EGL_NO_DISPLAY;
    m_gbm.reset();
    m_instance = {};
    m_getDeviceProcAddr = nullptr;
    m_enumerateDeviceExtensionProperties = nullptr;
    m_getPhysicalDeviceProperties2 = nullptr;
    m_getPhysicalDeviceFormatProperties2 = nullptr;
    m_getPhysicalDeviceImageFormatProperties2 = nullptr;
    m_getPhysicalDeviceExternalSemaphoreProperties = nullptr;
    m_getPhysicalDeviceMemoryProperties = nullptr;
    m_createImage = nullptr;
    m_destroyImage = nullptr;
    m_getImageMemoryRequirements = nullptr;
    m_allocateMemory = nullptr;
    m_freeMemory = nullptr;
    m_bindImageMemory = nullptr;
    m_createSemaphore = nullptr;
    m_destroySemaphore = nullptr;
    m_getMemoryFdProperties = nullptr;
    m_getSemaphoreFd = nullptr;
    m_initialized = false;
    m_frameNotifications.store(0);
    m_pendingFrameCount.store(0);
    m_readySequence = {{ 0, 0, 0 }};
    m_nextReadySequence = 0;
    m_nextToken.store(0);
    m_slotsChanging.store(false);
    for (auto& token : m_reservedTokens)
        token.store(0);
    for (auto& token : m_pendingCompletionTokens)
        token.store(0);
    m_forceDestroy = false;
}
