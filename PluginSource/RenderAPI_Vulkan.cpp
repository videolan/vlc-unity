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
#include <android/hardware_buffer_jni.h>

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
    a_hardware_buffer = other.a_hardware_buffer;
    other.a_hardware_buffer = nullptr;

    egl_image = other.egl_image;
    other.egl_image = EGL_NO_IMAGE_KHR;

    gl_texture = other.gl_texture;
    other.gl_texture = 0;

    fbo = other.fbo;
    other.fbo = 0;

    vk_image_external = other.vk_image_external;
    other.vk_image_external = VK_NULL_HANDLE;

    vk_memory_external = other.vk_memory_external;
    other.vk_memory_external = VK_NULL_HANDLE;

    vk_image_internal = other.vk_image_internal;
    other.vk_image_internal = VK_NULL_HANDLE;

    vk_memory_internal = other.vk_memory_internal;
    other.vk_memory_internal = VK_NULL_HANDLE;

    vk_image_view_internal = other.vk_image_view_internal;
    other.vk_image_view_internal = VK_NULL_HANDLE;

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
    DEBUG("[Vulkan] Entering Vulkan constructor (GPU copy approach)");
}

RenderAPI_Vulkan::~RenderAPI_Vulkan()
{
    releaseHardwareBufferResources();
    if (m_vk_command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_vk_instance.device, m_vk_command_pool, nullptr);
        m_vk_command_pool = VK_NULL_HANDLE;
    }
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
        DEBUG("[Vulkan] Entering ProcessDeviceEvent with kUnityGfxDeviceEventInitialize (GPU copy)");

        m_vk_graphics = interfaces->Get<IUnityGraphicsVulkan>();
        if (m_vk_graphics == nullptr) {
            DEBUG("[Vulkan] Could not retrieve IUnityGraphicsVulkan");
            return;
        }

        m_vk_instance = m_vk_graphics->Instance();
        DEBUG("[Vulkan] Unity Vulkan device: %p", m_vk_instance.device);

        // Get EGL extension pointers
        eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        eglGetNativeClientBufferANDROID = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");

        if (!eglCreateImageKHR || !eglDestroyImageKHR || !glEGLImageTargetTexture2DOES || !eglGetNativeClientBufferANDROID) {
            DEBUG("[Vulkan] Failed to get EGL extension pointers");
            return;
        }

        // Get Vulkan extension pointers
        vkGetAndroidHardwareBufferPropertiesANDROID = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetInstanceProcAddr(m_vk_instance.instance, "vkGetAndroidHardwareBufferPropertiesANDROID");
        if (!vkGetAndroidHardwareBufferPropertiesANDROID) {
            DEBUG("[Vulkan] Could not get vkGetAndroidHardwareBufferPropertiesANDROID proc address");
            return;
        }

        const EGLint config_attr[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE,   8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };

        const EGLint surface_attr[] = { EGL_WIDTH, 2, EGL_HEIGHT, 2, EGL_NONE };
        EGLConfig config;
        EGLint num_configs;

        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr) ||
            !eglChooseConfig(m_display, config_attr, &config, 1, &num_configs)) {
            DEBUG("[Vulkan] Failed to initialize EGL display: %x", eglGetError());
            return;
        }

        m_surface = eglCreatePbufferSurface(m_display, config, surface_attr);
        if (m_surface == EGL_NO_SURFACE) {
            DEBUG("[Vulkan] eglCreatePbufferSurface() failed: %x", eglGetError());
            return;
        }

        const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, ctx_attr);
        if (m_context == EGL_NO_CONTEXT) {
            DEBUG("[Vulkan] eglCreateContext() failed: %x", eglGetError());
            return;
        }

        DEBUG("[Vulkan] Created standalone OpenGL ES context: disp=%p surf=%p ctx=%p", m_display, m_surface, m_context);

        // Create Vulkan command pool
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = m_vk_instance.queueFamilyIndex;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(m_vk_instance.device, &pool_info, nullptr, &m_vk_command_pool) != VK_SUCCESS) {
            DEBUG("[Vulkan] Failed to create command pool");
            return;
        }

        // Allocate persistent command buffer
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = m_vk_command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_vk_instance.device, &alloc_info, &m_vk_command_buffer) != VK_SUCCESS) {
            DEBUG("[Vulkan] Failed to allocate command buffer");
            return;
        }

        DEBUG("[Vulkan] kUnityGfxDeviceEventInitialize success");
    }
    else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[Vulkan] kUnityGfxDeviceEventShutdown");
        releaseHardwareBufferResources();

        if (m_vk_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_vk_instance.device, m_vk_command_pool, nullptr);
            m_vk_command_pool = VK_NULL_HANDLE;
        }

        if (m_context != EGL_NO_CONTEXT) eglDestroyContext(m_display, m_context);
        if (m_surface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_surface);
        if (m_display != EGL_NO_DISPLAY) eglTerminate(m_display);
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

    if (cfg->width != that->width || cfg->height != that->height) {
        that->releaseHardwareBufferResources();
        that->width = cfg->width;
        that->height = cfg->height;

        for (auto& buffer : that->buffers)
            buffer = that->createHardwareBuffer(cfg->width, cfg->height);
    }

    output->opengl_format = GL_RGBA;
    output->full_range = true;
    output->colorspace = libvlc_video_colorspace_BT709;
    output->primaries  = libvlc_video_primaries_BT709;
    output->transfer   = libvlc_video_transfer_func_SRGB;
    output->orientation = libvlc_video_orient_bottom_right;

    glBindFramebuffer(GL_FRAMEBUFFER, that->buffers[that->idx_render].fbo);
    return true;
}

void RenderAPI_Vulkan::swap(void* opaque)
{
    DEBUG("[Vulkan] swap callback");
    RenderAPI_Vulkan* that = reinterpret_cast<RenderAPI_Vulkan*>(opaque);
    glFlush();
    glFinish();
    std::lock_guard<std::mutex> lock(that->text_lock);
    that->updated = true;
    std::swap(that->idx_swap, that->idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, that->buffers[that->idx_render].fbo);
    DEBUG("[Vulkan] swap callback complete");
}

RenderAPIHardwareBuffer RenderAPI_Vulkan::createHardwareBuffer(unsigned width, unsigned height)
{
    RenderAPIHardwareBuffer buffer;

    // 1. Create AHardwareBuffer
    AHardwareBuffer_Desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

    if (AHardwareBuffer_allocate(&desc, &buffer.a_hardware_buffer) != 0) {
        throw std::runtime_error("Failed to allocate AHardwareBuffer");
    }

    // 2. Create GL texture from AHardwareBuffer via EGLImage
    EGLClientBuffer client_buffer = eglGetNativeClientBufferANDROID(buffer.a_hardware_buffer);
    const EGLint egl_image_attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    buffer.egl_image = eglCreateImageKHR(m_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client_buffer, egl_image_attrs);

    if (buffer.egl_image == EGL_NO_IMAGE_KHR) {
        AHardwareBuffer_release(buffer.a_hardware_buffer);
        throw std::runtime_error("Failed to create EGLImage");
    }

    glGenTextures(1, &buffer.gl_texture);
    glBindTexture(GL_TEXTURE_2D, buffer.gl_texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, buffer.egl_image);

    // 3. Create framebuffer for VLC
    glGenFramebuffers(1, &buffer.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buffer.gl_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        AHardwareBuffer_release(buffer.a_hardware_buffer);
        throw std::runtime_error("Failed to create framebuffer");
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 4. Create Vulkan textures (external and internal)
    if (!createVulkanTexture(buffer, width, height)) {
        releaseHardwareBufferResources(); // Clean up partial resources
        throw std::runtime_error("Failed to create Vulkan texture");
    }

    DEBUG("[Vulkan] Created buffer: AHB=%p, GL tex=%u, VK ext=%p, VK int=%p",
          buffer.a_hardware_buffer, buffer.gl_texture, buffer.vk_image_external, buffer.vk_image_internal);

    return buffer;
}

bool RenderAPI_Vulkan::createVulkanTexture(RenderAPIHardwareBuffer& buffer, unsigned width, unsigned height)
{
    // 1. Create external VkImage from AHardwareBuffer
    VkExternalMemoryImageCreateInfo ext_mem_img_info = {};
    ext_mem_img_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext_mem_img_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &ext_mem_img_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_vk_instance.device, &image_info, nullptr, &buffer.vk_image_external) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to create external VkImage");
        return false;
    }

    // 2. Allocate memory for external image
    VkAndroidHardwareBufferPropertiesANDROID buf_props = {};
    buf_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    vkGetAndroidHardwareBufferPropertiesANDROID(m_vk_instance.device, buffer.a_hardware_buffer, &buf_props);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = buf_props.allocationSize;
    alloc_info.memoryTypeIndex = buf_props.memoryTypeBits;

    VkImportAndroidHardwareBufferInfoANDROID import_info = {};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import_info.buffer = buffer.a_hardware_buffer;
    alloc_info.pNext = &import_info;

    if (vkAllocateMemory(m_vk_instance.device, &alloc_info, nullptr, &buffer.vk_memory_external) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to allocate memory for external image");
        return false;
    }

    if (vkBindImageMemory(m_vk_instance.device, buffer.vk_image_external, buffer.vk_memory_external, 0) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to bind memory for external image");
        return false;
    }

    // 3. Create internal, Unity-compatible VkImage
    image_info.pNext = nullptr;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if (vkCreateImage(m_vk_instance.device, &image_info, nullptr, &buffer.vk_image_internal) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to create internal VkImage");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(m_vk_instance.device, buffer.vk_image_internal, &mem_reqs);

    // Find suitable memory type
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(m_vk_instance.physicalDevice, &mem_props);

    uint32_t memory_type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        DEBUG("[Vulkan] Could not find suitable memory type for internal image");
        return false;
    }

    // Allocate memory for internal image (with exportable flag)
    VkExportMemoryAllocateInfo export_alloc_info = {};
    export_alloc_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_alloc_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    alloc_info.pNext = &export_alloc_info;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(m_vk_instance.device, &alloc_info, nullptr, &buffer.vk_memory_internal) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to allocate memory for internal image");
        return false;
    }

    if (vkBindImageMemory(m_vk_instance.device, buffer.vk_image_internal, buffer.vk_memory_internal, 0) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to bind memory for internal image");
        return false;
    }

    // 4. Create image view for internal image
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = buffer.vk_image_internal;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_vk_instance.device, &view_info, nullptr, &buffer.vk_image_view_internal) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to create image view for internal image");
        return false;
    }

    // Transition layout to a known state
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_vk_command_buffer, &begin_info);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = buffer.vk_image_internal;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(m_vk_command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(m_vk_command_buffer);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &m_vk_command_buffer;
    vkQueueSubmit(m_vk_instance.graphicsQueue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_vk_instance.graphicsQueue);

    return true;
}

void RenderAPI_Vulkan::copyVulkan(const RenderAPIHardwareBuffer& buffer)
{
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_vk_command_buffer, &begin_info);

    // Transition layouts for blit
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // External to SRC
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = buffer.vk_image_external;
    vkCmdPipelineBarrier(m_vk_command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Internal to DST
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = buffer.vk_image_internal;
    vkCmdPipelineBarrier(m_vk_command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Blit
    VkImageBlit blit = {};
    blit.srcOffsets[1] = { (int32_t)width, (int32_t)height, 1 };
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[1] = { (int32_t)width, (int32_t)height, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    vkCmdBlitImage(m_vk_command_buffer, buffer.vk_image_external, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   buffer.vk_image_internal, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    // Transition layouts back
    // External to GENERAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    barrier.image = buffer.vk_image_external;
    vkCmdPipelineBarrier(m_vk_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Internal to SHADER_READ
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.image = buffer.vk_image_internal;
    vkCmdPipelineBarrier(m_vk_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(m_vk_command_buffer);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &m_vk_command_buffer;
    vkQueueSubmit(m_vk_instance.graphicsQueue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_vk_instance.graphicsQueue);
}

void* RenderAPI_Vulkan::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    (void)width; (void)height;
    std::lock_guard<std::mutex> lock(text_lock);

    if (out_updated)
        *out_updated = updated;

    if (updated) {
        DEBUG("[Vulkan] Frame updated, performing GPU copy");
        std::swap(idx_swap, idx_display);
        updated = false;
        copyVulkan(buffers[idx_display]);
    }

    VkImage img = buffers[idx_display].vk_image_internal;
    DEBUG("[Vulkan] Returning VkImage: %p (idx=%zu)", img, idx_display);
    return reinterpret_cast<void*>(img);
}

void RenderAPI_Vulkan::releaseHardwareBufferResources()
{
    vkQueueWaitIdle(m_vk_instance.graphicsQueue);

    for (auto& buffer : buffers) {
        if (buffer.vk_image_view_internal != VK_NULL_HANDLE) {
            vkDestroyImageView(m_vk_instance.device, buffer.vk_image_view_internal, nullptr);
            buffer.vk_image_view_internal = VK_NULL_HANDLE;
        }
        if (buffer.vk_image_internal != VK_NULL_HANDLE) {
            vkDestroyImage(m_vk_instance.device, buffer.vk_image_internal, nullptr);
            buffer.vk_image_internal = VK_NULL_HANDLE;
        }
        if (buffer.vk_memory_internal != VK_NULL_HANDLE) {
            vkFreeMemory(m_vk_instance.device, buffer.vk_memory_internal, nullptr);
            buffer.vk_memory_internal = VK_NULL_HANDLE;
        }
        if (buffer.vk_image_external != VK_NULL_HANDLE) {
            vkDestroyImage(m_vk_instance.device, buffer.vk_image_external, nullptr);
            buffer.vk_image_external = VK_NULL_HANDLE;
        }
        if (buffer.vk_memory_external != VK_NULL_HANDLE) {
            vkFreeMemory(m_vk_instance.device, buffer.vk_memory_external, nullptr);
            buffer.vk_memory_external = VK_NULL_HANDLE;
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
            eglDestroyImageKHR(m_display, buffer.egl_image);
            buffer.egl_image = EGL_NO_IMAGE_KHR;
        }
        if (buffer.a_hardware_buffer != nullptr) {
            AHardwareBuffer_release(buffer.a_hardware_buffer);
            buffer.a_hardware_buffer = nullptr;
        }
    }
}

#endif // UNITY_ANDROID
