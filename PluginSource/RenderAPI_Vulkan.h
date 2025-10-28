#ifndef RENDER_API_VULKAN_H
#define RENDER_API_VULKAN_H

#include "RenderAPI.h"
#include "RenderAPI_OpenGLEGL.h"
#include "PlatformBase.h"

#if defined(UNITY_ANDROID)
#include <android/hardware_buffer.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <jni.h>

// Define platform before including Vulkan headers
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#include <vulkan/vulkan.h>

#include "Unity/IUnityGraphicsVulkan.h"
#include <mutex>

// ============================================================
// Vulkan Debug/Validation Configuration
// ============================================================
// Set to 1 to enable verbose logging for successful operations
// Set to 0 to only log errors and high-level entry/exit points
#ifndef VULKAN_VERBOSE_SUCCESS_LOGS
#define VULKAN_VERBOSE_SUCCESS_LOGS 0
#endif

// Set to 1 to enable Vulkan validation layers
// Set to 0 to disable validation (better performance)
#ifndef VULKAN_ENABLE_VALIDATION
#define VULKAN_ENABLE_VALIDATION 0
#endif
// ============================================================

typedef struct libvlc_media_player_t libvlc_media_player_t;

// AHardwareBuffer requires API level 26+
#define AHARDWAREBUFFER_MIN_API 26

// GPU copy approach: GL texture for VLC rendering, VK texture for Unity display
struct RenderAPIHardwareBuffer
{
    // Shared Android hardware buffer
    AHardwareBuffer* a_hardware_buffer = nullptr;

    // EGL side (for creating GL texture from AHardwareBuffer)
    EGLImageKHR egl_image = EGL_NO_IMAGE_KHR;

    // OpenGL ES side (for VLC rendering)
    GLuint gl_texture = 0;
    GLuint fbo = 0;

    // Vulkan external image (imported from AHardwareBuffer)
    VkImage vk_image_external = VK_NULL_HANDLE;
    VkDeviceMemory vk_memory_external = VK_NULL_HANDLE;

    ~RenderAPIHardwareBuffer();

    RenderAPIHardwareBuffer& operator=(RenderAPIHardwareBuffer &&other);
    RenderAPIHardwareBuffer(){}
    RenderAPIHardwareBuffer(RenderAPIHardwareBuffer &&other);
};

class RenderAPI_Vulkan : public RenderAPI_OpenEGL
{
public:
    RenderAPI_Vulkan(UnityGfxRenderer apiType);
    ~RenderAPI_Vulkan() override;

    void setVlcContext(libvlc_media_player_t *mp) override;
    void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
    void* getVideoFrame(unsigned width, unsigned height, bool* out_updated) override;

    // Vulkan-specific: Set Unity-created texture to update via AccessTexture
    bool setUnityTexture(void* unityTexturePtr);

    // Called from Unity render thread to perform texture copy
    void onRenderEvent();

    static bool setup(void **opaque, const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out);
    static void cleanup(void* opaque);
    static bool resize(void* opaque, const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *output);
    static void swap(void* opaque);

    // Validation layer support
    static void InitializeValidationLayers(IUnityInterfaces* interfaces);

private:
    jobject createWindowSurface();
    void destroyWindowSurface(jobject);
    jobject m_awindow = nullptr;

private:
    void releaseHardwareBufferResources();
    RenderAPIHardwareBuffer createHardwareBuffer(unsigned width, unsigned height);
    bool createVulkanTexture(RenderAPIHardwareBuffer& buffer, unsigned width, unsigned height);
    bool copyToUnityTexture(const RenderAPIHardwareBuffer& buffer);

    // Vulkan state from Unity
    UnityVulkanInstance m_vk_instance;
    IUnityGraphicsVulkan* m_vk_graphics = nullptr;

    // Unity-created texture pointer (for Vulkan AccessTexture approach)
    void* m_unity_texture_ptr = nullptr;

    // Track if Vulkan image layout has been initialized
    bool m_vulkan_image_layout_initialized = false;

    // Pending copy state (for render thread)
    size_t m_pending_copy_buffer_idx = 0;
    bool m_has_pending_copy = false;

    // Triple buffering
    RenderAPIHardwareBuffer buffers[3];

    std::mutex text_lock;
    unsigned width = 0;
    unsigned height = 0;
    size_t idx_render = 0;
    size_t idx_swap = 1;
    size_t idx_display = 2;
    bool updated = false;

    // EGL/GLES extension function pointers
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID = nullptr;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;

    // Vulkan extension function pointers for AHardwareBuffer
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID = nullptr;

#if VULKAN_ENABLE_VALIDATION
    // Validation layer support
    static VkDebugUtilsMessengerEXT s_debug_messenger;
    static PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
    static PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
#endif
};

#endif // UNITY_ANDROID

#endif /* RENDER_API_VULKAN_H */
