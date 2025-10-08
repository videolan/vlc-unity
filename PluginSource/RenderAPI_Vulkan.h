#ifndef RENDER_API_VULKAN_H
#define RENDER_API_VULKAN_H

#include "RenderAPI.h"
#include "RenderAPI_OpenGLEGL.h"
#include "PlatformBase.h"

#if defined(UNITY_ANDROID)
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// Define platform before including Vulkan headers
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#include <vulkan/vulkan.h>

#include "Unity/IUnityGraphicsVulkan.h"
#include <mutex>

typedef struct libvlc_media_player_t libvlc_media_player_t;

// AHardwareBuffer bridge structure
struct RenderAPIHardwareBuffer
{
    AHardwareBuffer* ahb = nullptr;

    // OpenGL ES side (for VLC rendering)
    EGLImageKHR egl_image = EGL_NO_IMAGE_KHR;
    GLuint gl_texture = 0;
    GLuint fbo = 0;

    // Vulkan side (for Unity display)
    VkImage vk_image = VK_NULL_HANDLE;
    VkDeviceMemory vk_memory = VK_NULL_HANDLE;
    VkImageView vk_image_view = VK_NULL_HANDLE;

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

private:
    jobject createWindowSurface();
    void destroyWindowSurface(jobject);
    jobject m_awindow = nullptr;

    static bool setup(void **opaque, const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out);
    static void cleanup(void* opaque);
    static bool resize(void* opaque, const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *output);
    static void swap(void* opaque);

private:
    void releaseHardwareBufferResources();
    RenderAPIHardwareBuffer createHardwareBuffer(unsigned width, unsigned height);
    bool createEGLImage(RenderAPIHardwareBuffer& buffer);
    bool createVulkanImage(RenderAPIHardwareBuffer& buffer, unsigned width, unsigned height);

    // Vulkan state from Unity
    UnityVulkanInstance m_vk_instance;
    IUnityGraphicsVulkan* m_vk_graphics = nullptr;

    // EGL extensions
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID = nullptr;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;

    // Vulkan extensions
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID = nullptr;
    PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID = nullptr;

    // Triple buffering
    RenderAPIHardwareBuffer buffers[3];

    std::mutex text_lock;
    unsigned width = 0;
    unsigned height = 0;
    size_t idx_render = 0;
    size_t idx_swap = 1;
    size_t idx_display = 2;
    bool updated = false;
};

#endif // UNITY_ANDROID

#endif /* RENDER_API_VULKAN_H */
