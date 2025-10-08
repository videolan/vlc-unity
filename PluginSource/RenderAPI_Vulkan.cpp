/*****************************************************************************
 * RenderAPI_Vulkan.cpp: Android Vulkan renderer through OpenGL ES
 *****************************************************************************
 * Copyright (C) 2025 Videolabs
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#if defined(UNITY_ANDROID)

#include "RenderAPI_Vulkan.h"
#include "Log.h"
#include <cassert>
#include <stdexcept>
#include <jni.h>
#include <vlc/libvlc_media_player.h>

// AHardwareBuffer requires API level 26+
#define AHARDWAREBUFFER_MIN_API 26

// External JNI environment from RenderAPI_Android.cpp
extern JNIEnv* jni_env;

namespace {

bool staticMakeCurrent(void* data, bool current)
{
    auto that = static_cast<RenderAPI_Vulkan*>(data);
    return that->makeCurrent(current);
}

void staticSwap(void *data)
{
    RenderAPI_Vulkan::swap(data);
}

}

RenderAPI* CreateRenderAPI_Vulkan(UnityGfxRenderer apiType)
{
    return new RenderAPI_Vulkan(apiType);
}

// RenderAPIHardwareBuffer implementation
RenderAPIHardwareBuffer::~RenderAPIHardwareBuffer()
{
    // Cleanup happens in releaseHardwareBufferResources
}

RenderAPIHardwareBuffer& RenderAPIHardwareBuffer::operator=(RenderAPIHardwareBuffer &&other)
{
    ahb = other.ahb;
    other.ahb = nullptr;

    egl_image = other.egl_image;
    other.egl_image = EGL_NO_IMAGE_KHR;

    gl_texture = other.gl_texture;
    other.gl_texture = 0;

    fbo = other.fbo;
    other.fbo = 0;

    vk_image = other.vk_image;
    other.vk_image = VK_NULL_HANDLE;

    vk_memory = other.vk_memory;
    other.vk_memory = VK_NULL_HANDLE;

    vk_image_view = other.vk_image_view;
    other.vk_image_view = VK_NULL_HANDLE;

    return *this;
}

RenderAPIHardwareBuffer::RenderAPIHardwareBuffer(RenderAPIHardwareBuffer &&other)
{
    *this = std::forward<RenderAPIHardwareBuffer>(other);
}

// RenderAPI_Vulkan implementation
RenderAPI_Vulkan::RenderAPI_Vulkan(UnityGfxRenderer apiType)
    : RenderAPI_OpenEGL(apiType)
{
    DEBUG("[Vulkan] Entering Vulkan constructor");

    // Check API level
    if (__ANDROID_API__ < AHARDWAREBUFFER_MIN_API) {
        DEBUG("[Vulkan] AHardwareBuffer requires API 26+, current: %d", __ANDROID_API__);
        throw std::runtime_error("AHardwareBuffer requires Android API 26+");
    }
}

RenderAPI_Vulkan::~RenderAPI_Vulkan()
{
    releaseHardwareBufferResources();
    if (m_awindow)
        destroyWindowSurface(m_awindow);
}

jobject RenderAPI_Vulkan::createWindowSurface()
{
    DEBUG("[Vulkan] Entering createWindowSurface");

    jclass activityThread = jni_env->FindClass("android/app/ActivityThread");
    jmethodID currentApplication = jni_env->GetStaticMethodID(activityThread, "currentApplication", "()Landroid/app/Application;");
    jobject app = jni_env->CallStaticObjectMethod(activityThread, currentApplication);

    jclass contextClass = jni_env->FindClass("android/content/Context");
    jmethodID getClassLoader = jni_env->GetMethodID(contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject classLoader = jni_env->CallObjectMethod(app, getClassLoader);

    jclass classLoaderClass = jni_env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = jni_env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

    jstring className = jni_env->NewStringUTF("org.videolan.libvlc.AWindow");
    jclass cls_JavaClass = (jclass)jni_env->CallObjectMethod(classLoader, loadClass, className);
    jni_env->DeleteLocalRef(className);

    if(cls_JavaClass == nullptr)
    {
        DEBUG("[Vulkan] Failed to find class org.videolan.libvlc.AWindow");
        return nullptr;
    }

    jmethodID mid_JavaClass = jni_env->GetMethodID(cls_JavaClass, "<init>", "(Lorg/videolan/libvlc/AWindow$SurfaceCallback;)V");
    jobject obj_JavaClass = jni_env->NewObject(cls_JavaClass, mid_JavaClass, nullptr);

    return jni_env->NewGlobalRef(obj_JavaClass);
}

void RenderAPI_Vulkan::destroyWindowSurface(jobject obj)
{
    if (obj != nullptr)
        jni_env->DeleteGlobalRef(obj);
}

void RenderAPI_Vulkan::setVlcContext(libvlc_media_player_t *mp)
{
    if(RenderAPI_OpenEGL::unity_context == EGL_NO_CONTEXT) {
        DEBUG("[Vulkan] No OpenGL context retrieved");
        return;
    }

    DEBUG("[Vulkan] setVlcContext %p", this);

    // Create AWindow for hardware-accelerated decoding (MediaCodec)
    if (m_awindow == nullptr)
        m_awindow = createWindowSurface();

    if (m_awindow != nullptr)
        libvlc_media_player_set_android_context(mp, m_awindow);
    else
        DEBUG("[Vulkan] can't create window surface for media codec");

    DEBUG("[Vulkan] subscribing to opengl output callbacks %p", this);
    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_gles2,
        setup, cleanup, nullptr, resize, staticSwap,
        staticMakeCurrent, RenderAPI_OpenEGL::get_proc_address, nullptr, nullptr, this);
}

void RenderAPI_Vulkan::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    if (type == kUnityGfxDeviceEventInitialize) {
        DEBUG("[Vulkan] Entering ProcessDeviceEvent with kUnityGfxDeviceEventInitialize");

        // Get Vulkan interface from Unity
        m_vk_graphics = interfaces->Get<IUnityGraphicsVulkan>();
        if (m_vk_graphics == nullptr) {
            DEBUG("[Vulkan] Could not retrieve IUnityGraphicsVulkan");
            return;
        }

        // Get Vulkan instance from Unity
        m_vk_instance = m_vk_graphics->Instance();
        DEBUG("[Vulkan] Unity Vulkan device: %p", m_vk_instance.device);

        // Load EGL extensions for AHardwareBuffer
        eglGetNativeClientBufferANDROID = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
            eglGetProcAddress("eglGetNativeClientBufferANDROID");
        eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
            eglGetProcAddress("eglCreateImageKHR");
        eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
            eglGetProcAddress("eglDestroyImageKHR");
        glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");

        if (!eglGetNativeClientBufferANDROID || !eglCreateImageKHR ||
            !eglDestroyImageKHR || !glEGLImageTargetTexture2DOES) {
            DEBUG("[Vulkan] Failed to load EGL extensions for AHardwareBuffer");
            return;
        }

        // Load Vulkan extensions for AHardwareBuffer
        vkGetAndroidHardwareBufferPropertiesANDROID =
            (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)
            m_vk_instance.getInstanceProcAddr(m_vk_instance.instance,
                "vkGetAndroidHardwareBufferPropertiesANDROID");
        vkGetMemoryAndroidHardwareBufferANDROID =
            (PFN_vkGetMemoryAndroidHardwareBufferANDROID)
            m_vk_instance.getInstanceProcAddr(m_vk_instance.instance,
                "vkGetMemoryAndroidHardwareBufferANDROID");

        if (!vkGetAndroidHardwareBufferPropertiesANDROID ||
            !vkGetMemoryAndroidHardwareBufferANDROID) {
            DEBUG("[Vulkan] Failed to load Vulkan AHardwareBuffer extensions");
            return;
        }

        DEBUG("[Vulkan] kUnityGfxDeviceEventInitialize success");
    }
    else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[Vulkan] kUnityGfxDeviceEventShutdown");
        releaseHardwareBufferResources();
    }
}

bool RenderAPI_Vulkan::setup(void **opaque, const libvlc_video_setup_device_cfg_t *cfg,
                              libvlc_video_setup_device_info_t *out)
{
    (void)cfg; (void)out;
    DEBUG("[Vulkan] output callback setup");

    auto *that = static_cast<RenderAPI_Vulkan*>(*opaque);
    that->width = 0;
    that->height = 0;

    return true;
}

void RenderAPI_Vulkan::cleanup(void* opaque)
{
    DEBUG("[Vulkan] output callback cleanup");

    auto *that = static_cast<RenderAPI_Vulkan*>(opaque);
    that->releaseHardwareBufferResources();
}

bool RenderAPI_Vulkan::resize(void* opaque, const libvlc_video_render_cfg_t *cfg,
                               libvlc_video_output_cfg_t *output)
{
    DEBUG("[Vulkan] call resize %dx%d", cfg->width, cfg->height);
    auto *that = static_cast<RenderAPI_Vulkan*>(opaque);

    if (cfg->width != that->width || cfg->height != that->height)
        that->releaseHardwareBufferResources();

    // Create hardware buffers
    for (auto& buffer : that->buffers)
        buffer = that->createHardwareBuffer(cfg->width, cfg->height);

    that->width = cfg->width;
    that->height = cfg->height;

    // Set VLC output configuration
    output->opengl_format = GL_RGBA;
    output->full_range = true;
    output->colorspace = libvlc_video_colorspace_BT709;
    output->primaries  = libvlc_video_primaries_BT709;
    output->transfer   = libvlc_video_transfer_func_SRGB;
    output->orientation = libvlc_video_orient_bottom_right;

    // Bind first FBO for VLC to render into
    glBindFramebuffer(GL_FRAMEBUFFER, that->buffers[that->idx_render].fbo);

    return true;
}

void RenderAPI_Vulkan::swap(void* opaque)
{
    RenderAPI_Vulkan* that = reinterpret_cast<RenderAPI_Vulkan*>(opaque);
    std::lock_guard<std::mutex> lock(that->text_lock);
    that->updated = true;

    std::swap(that->idx_swap, that->idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, that->buffers[that->idx_render].fbo);
}

RenderAPIHardwareBuffer RenderAPI_Vulkan::createHardwareBuffer(unsigned width, unsigned height)
{
    RenderAPIHardwareBuffer buffer;

    // Create AHardwareBuffer
    AHardwareBuffer_Desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                 AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

    int ret = AHardwareBuffer_allocate(&desc, &buffer.ahb);
    if (ret != 0) {
        DEBUG("[Vulkan] AHardwareBuffer_allocate failed: %d", ret);
        throw std::runtime_error("Failed to allocate AHardwareBuffer");
    }

    // Create EGL image and GL texture from AHardwareBuffer
    if (!createEGLImage(buffer)) {
        AHardwareBuffer_release(buffer.ahb);
        throw std::runtime_error("Failed to create EGL image");
    }

    // Create Vulkan image from AHardwareBuffer
    if (!createVulkanImage(buffer, width, height)) {
        if (buffer.egl_image != EGL_NO_IMAGE_KHR)
            eglDestroyImageKHR(eglGetCurrentDisplay(), buffer.egl_image);
        if (buffer.gl_texture != 0)
            glDeleteTextures(1, &buffer.gl_texture);
        AHardwareBuffer_release(buffer.ahb);
        throw std::runtime_error("Failed to create Vulkan image");
    }

    DEBUG("[Vulkan] Created hardware buffer: ahb=%p gl_tex=%u vk_img=%p",
          buffer.ahb, buffer.gl_texture, buffer.vk_image);

    return buffer;
}

bool RenderAPI_Vulkan::createEGLImage(RenderAPIHardwareBuffer& buffer)
{
    // Get EGL client buffer from AHardwareBuffer
    EGLClientBuffer egl_buffer = eglGetNativeClientBufferANDROID(buffer.ahb);
    if (egl_buffer == nullptr) {
        DEBUG("[Vulkan] eglGetNativeClientBufferANDROID failed");
        return false;
    }

    // Create EGL image
    EGLint egl_image_attrs[] = { EGL_NONE };
    buffer.egl_image = eglCreateImageKHR(
        eglGetCurrentDisplay(),
        EGL_NO_CONTEXT,
        EGL_NATIVE_BUFFER_ANDROID,
        egl_buffer,
        egl_image_attrs
    );

    if (buffer.egl_image == EGL_NO_IMAGE_KHR) {
        DEBUG("[Vulkan] eglCreateImageKHR failed: %x", eglGetError());
        return false;
    }

    // Create GL texture from EGL image
    glGenTextures(1, &buffer.gl_texture);
    glBindTexture(GL_TEXTURE_2D, buffer.gl_texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, buffer.egl_image);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Create framebuffer for VLC to render into
    glGenFramebuffers(1, &buffer.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          buffer.gl_texture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DEBUG("[Vulkan] Framebuffer incomplete: %x", status);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

bool RenderAPI_Vulkan::createVulkanImage(RenderAPIHardwareBuffer& buffer,
                                          unsigned width, unsigned height)
{
    VkResult vkres;

    // Get AHardwareBuffer properties
    VkAndroidHardwareBufferPropertiesANDROID ahb_props = {};
    ahb_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;

    vkres = vkGetAndroidHardwareBufferPropertiesANDROID(
        m_vk_instance.device, buffer.ahb, &ahb_props);

    if (vkres != VK_SUCCESS) {
        DEBUG("[Vulkan] vkGetAndroidHardwareBufferPropertiesANDROID failed: %d", vkres);
        return false;
    }

    // Create VkImage from AHardwareBuffer
    VkExternalMemoryImageCreateInfo external_info = {};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = { width, height, 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkres = vkCreateImage(m_vk_instance.device, &image_info, nullptr, &buffer.vk_image);
    if (vkres != VK_SUCCESS) {
        DEBUG("[Vulkan] vkCreateImage failed: %d", vkres);
        return false;
    }

    // Import AHardwareBuffer as device memory
    VkImportAndroidHardwareBufferInfoANDROID import_info = {};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import_info.buffer = buffer.ahb;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &import_info;
    alloc_info.allocationSize = ahb_props.allocationSize;

    // Find suitable memory type
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(m_vk_instance.device, buffer.vk_image, &mem_reqs);

    uint32_t memory_type_index = 0;
    for (; memory_type_index < 32; memory_type_index++) {
        if ((ahb_props.memoryTypeBits & (1 << memory_type_index)) &&
            (mem_reqs.memoryTypeBits & (1 << memory_type_index))) {
            break;
        }
    }
    alloc_info.memoryTypeIndex = memory_type_index;

    vkres = vkAllocateMemory(m_vk_instance.device, &alloc_info, nullptr, &buffer.vk_memory);
    if (vkres != VK_SUCCESS) {
        DEBUG("[Vulkan] vkAllocateMemory failed: %d", vkres);
        vkDestroyImage(m_vk_instance.device, buffer.vk_image, nullptr);
        buffer.vk_image = VK_NULL_HANDLE;
        return false;
    }

    // Bind memory to image
    vkres = vkBindImageMemory(m_vk_instance.device, buffer.vk_image, buffer.vk_memory, 0);
    if (vkres != VK_SUCCESS) {
        DEBUG("[Vulkan] vkBindImageMemory failed: %d", vkres);
        vkFreeMemory(m_vk_instance.device, buffer.vk_memory, nullptr);
        vkDestroyImage(m_vk_instance.device, buffer.vk_image, nullptr);
        buffer.vk_image = VK_NULL_HANDLE;
        buffer.vk_memory = VK_NULL_HANDLE;
        return false;
    }

    // Create image view
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = buffer.vk_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    vkres = vkCreateImageView(m_vk_instance.device, &view_info, nullptr, &buffer.vk_image_view);
    if (vkres != VK_SUCCESS) {
        DEBUG("[Vulkan] vkCreateImageView failed: %d", vkres);
        vkFreeMemory(m_vk_instance.device, buffer.vk_memory, nullptr);
        vkDestroyImage(m_vk_instance.device, buffer.vk_image, nullptr);
        buffer.vk_image = VK_NULL_HANDLE;
        buffer.vk_memory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void* RenderAPI_Vulkan::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    (void)width; (void)height;
    std::lock_guard<std::mutex> lock(text_lock);

    if (out_updated)
        *out_updated = updated;

    if (updated) {
        std::swap(idx_swap, idx_display);
        updated = false;
    }

    // Return Vulkan image for Unity to display
    return reinterpret_cast<void*>(buffers[idx_display].vk_image);
}

void RenderAPI_Vulkan::releaseHardwareBufferResources()
{
    for (auto& buffer : buffers) {
        if (buffer.vk_image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_vk_instance.device, buffer.vk_image_view, nullptr);
            buffer.vk_image_view = VK_NULL_HANDLE;
        }

        if (buffer.vk_image != VK_NULL_HANDLE) {
            vkDestroyImage(m_vk_instance.device, buffer.vk_image, nullptr);
            buffer.vk_image = VK_NULL_HANDLE;
        }

        if (buffer.vk_memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_vk_instance.device, buffer.vk_memory, nullptr);
            buffer.vk_memory = VK_NULL_HANDLE;
        }

        if (buffer.fbo != 0) {
            glDeleteFramebuffers(1, &buffer.fbo);
            buffer.fbo = 0;
        }

        if (buffer.gl_texture != 0) {
            glDeleteTextures(1, &buffer.gl_texture);
            buffer.gl_texture = 0;
        }

        if (buffer.egl_image != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR(eglGetCurrentDisplay(), buffer.egl_image);
            buffer.egl_image = EGL_NO_IMAGE_KHR;
        }

        if (buffer.ahb != nullptr) {
            AHardwareBuffer_release(buffer.ahb);
            buffer.ahb = nullptr;
        }
    }
}

#endif // UNITY_ANDROID
