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
#include <vector>
#include <jni.h>
#include <vlc/libvlc_media_player.h>
#include <android/hardware_buffer_jni.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>

// Debug/validation configuration is in RenderAPI_Vulkan.h

#if VULKAN_VERBOSE_SUCCESS_LOGS
#define DEBUG_VERBOSE(...) DEBUG(__VA_ARGS__)
#else
#define DEBUG_VERBOSE(...) ((void)0)
#endif

// External JNI environment from RenderAPI_Android.cpp
extern JNIEnv* jni_env;

#if VULKAN_ENABLE_VALIDATION
// Static members for validation layers
VkDebugUtilsMessengerEXT RenderAPI_Vulkan::s_debug_messenger = VK_NULL_HANDLE;
PFN_vkCreateDebugUtilsMessengerEXT RenderAPI_Vulkan::vkCreateDebugUtilsMessengerEXT = nullptr;
PFN_vkDestroyDebugUtilsMessengerEXT RenderAPI_Vulkan::vkDestroyDebugUtilsMessengerEXT = nullptr;
#endif

namespace {

#if VULKAN_ENABLE_VALIDATION
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    (void)messageType;  // Unused
    (void)pUserData;    // Unused

    const char* severity = "UNKNOWN";
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        severity = "ERROR";
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        severity = "WARNING";
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        severity = "INFO";
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        severity = "VERBOSE";

    DEBUG("[VALIDATION][%s] %s", severity, pCallbackData->pMessage);
    return VK_FALSE;
}
#endif // VULKAN_ENABLE_VALIDATION

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
    if (m_awindow)
        destroyWindowSurface(m_awindow);

#if VULKAN_ENABLE_VALIDATION
    // Cleanup validation messenger
    if (s_debug_messenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT) {
        vkDestroyDebugUtilsMessengerEXT(m_vk_instance.instance, s_debug_messenger, nullptr);
        s_debug_messenger = VK_NULL_HANDLE;
    }
#endif
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

bool RenderAPI_Vulkan::setup(void **opaque, const libvlc_video_setup_device_cfg_t *cfg,
                              libvlc_video_setup_device_info_t *out)
{
    (void)cfg; (void)out;
    DEBUG("[Vulkan] output callback setup");
    auto *that = static_cast<RenderAPI_Vulkan*>(*opaque);
    that->width = 0;
    that->height = 0;

    bool ret = true;

#if defined(SHOW_WATERMARK)
    that->makeCurrent(true);

    ret &= that->watermark.setup();

    that->makeCurrent(false);
#endif

    return ret;
}

void RenderAPI_Vulkan::cleanup(void* opaque)
{
    DEBUG("[Vulkan] output callback cleanup");
    auto *that = static_cast<RenderAPI_Vulkan*>(opaque);

    that->ensureCurrentContext();
    that->releaseHardwareBufferResources();

#if defined(SHOW_WATERMARK)
    that->watermark.cleanup();
#endif
    that->makeCurrent(false);
}

bool RenderAPI_Vulkan::resize(void* opaque, const libvlc_video_render_cfg_t *cfg,
                               libvlc_video_output_cfg_t *output)
{
    DEBUG("[Vulkan] call resize %dx%d", cfg->width, cfg->height);
    auto *that = static_cast<RenderAPI_Vulkan*>(opaque);

    if (cfg->width != that->width || cfg->height != that->height) {
        DEBUG("[Vulkan] Size changed, recreating buffers");
        that->releaseHardwareBufferResources();
        that->width = cfg->width;
        that->height = cfg->height;

        // Make GL context current before creating GL resources
        if (!that->makeCurrent(true)) {
            DEBUG("[Vulkan] Failed to make GL context current");
            return false;
        }

        DEBUG("[Vulkan] Creating 3 hardware buffers...");
        try {
            for (size_t i = 0; i < 3; i++) {
                DEBUG("[Vulkan] Creating buffer %zu/3", i+1);
                that->buffers[i] = that->createHardwareBuffer(cfg->width, cfg->height);
            }
            DEBUG("[Vulkan] All buffers created successfully");
        } catch (const std::exception& e) {
            DEBUG("[Vulkan] Exception during buffer creation: %s", e.what());
            return false;
        }
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
    DEBUG_VERBOSE("[Vulkan] swap callback");
    RenderAPI_Vulkan* that = reinterpret_cast<RenderAPI_Vulkan*>(opaque);
    std::lock_guard<std::mutex> lock(that->text_lock);
    that->updated = true;

#if defined(SHOW_WATERMARK)
    that->watermark.draw(that->buffers[that->idx_render].fbo, that->width, that->height);
#endif

    glFlush();
    std::swap(that->idx_swap, that->idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, that->buffers[that->idx_render].fbo);
    DEBUG_VERBOSE("[Vulkan] swap callback complete");
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

    // 4. Create Vulkan external image from AHardwareBuffer
    if (!createVulkanTexture(buffer, width, height)) {
        releaseHardwareBufferResources(); // Clean up partial resources
        throw std::runtime_error("Failed to create Vulkan texture");
    }

    DEBUG("[Vulkan] Created buffer: AHB=%p, GL tex=%u, VK ext=%p",
          buffer.a_hardware_buffer, buffer.gl_texture, buffer.vk_image_external);

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

    // 2. Allocate dedicated memory for external image
    VkAndroidHardwareBufferPropertiesANDROID buf_props = {};
    buf_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    vkGetAndroidHardwareBufferPropertiesANDROID(m_vk_instance.device, buffer.a_hardware_buffer, &buf_props);

    uint32_t memory_type_index = 0;
    bool found_mem_type = false;
    for (uint32_t i = 0; i < 32; i++) {
        if ((buf_props.memoryTypeBits & (1 << i)) != 0) {
            memory_type_index = i;
            found_mem_type = true;
            break;
        }
    }
    if (!found_mem_type) {
        DEBUG("[Vulkan] Could not find a compatible memory type for AHardwareBuffer");
        return false;
    }

    VkMemoryDedicatedAllocateInfo dedicated_alloc_info = {};
    dedicated_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_alloc_info.image = buffer.vk_image_external;

    VkImportAndroidHardwareBufferInfoANDROID import_info = {};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import_info.pNext = &dedicated_alloc_info;
    import_info.buffer = buffer.a_hardware_buffer;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &import_info;
    alloc_info.allocationSize = buf_props.allocationSize;
    alloc_info.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(m_vk_instance.device, &alloc_info, nullptr, &buffer.vk_memory_external) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to allocate memory for external image");
        return false;
    }

    if (vkBindImageMemory(m_vk_instance.device, buffer.vk_image_external, buffer.vk_memory_external, 0) != VK_SUCCESS) {
        DEBUG("[Vulkan] Failed to bind memory for external image");
        return false;
    }

    return true;
}

bool RenderAPI_Vulkan::setUnityTexture(void* unityTexturePtr)
{
    DEBUG("[Vulkan] === setUnityTexture called ===");
    DEBUG("[Vulkan]   New texture pointer: %p", unityTexturePtr);
    DEBUG("[Vulkan]   Previous texture pointer: %p", m_unity_texture_ptr);
    DEBUG("[Vulkan]   Current thread ID: %ld", (long)pthread_self());

    if (unityTexturePtr == nullptr) {
        DEBUG("[Vulkan] setUnityTexture called with nullptr");
        return false;
    }

    if (m_unity_texture_ptr != nullptr && m_unity_texture_ptr != unityTexturePtr) {
        DEBUG("[Vulkan] WARNING: Unity texture pointer changed from %p to %p", m_unity_texture_ptr, unityTexturePtr);
    }

    DEBUG("[Vulkan] Setting Unity texture: %p", unityTexturePtr);
    m_unity_texture_ptr = unityTexturePtr;
    return true;
}

bool RenderAPI_Vulkan::copyToUnityTexture(const RenderAPIHardwareBuffer& buffer)
{
    DEBUG_VERBOSE("[Vulkan] === copyToUnityTexture START ===");
    DEBUG_VERBOSE("[Vulkan]   Unity texture pointer: %p", m_unity_texture_ptr);
    DEBUG_VERBOSE("[Vulkan]   VK graphics interface: %p", m_vk_graphics);
    DEBUG_VERBOSE("[Vulkan]   VLC VkImage external: %p", buffer.vk_image_external);
    DEBUG_VERBOSE("[Vulkan]   AHardwareBuffer: %p", buffer.a_hardware_buffer);
    DEBUG_VERBOSE("[Vulkan]   Current thread ID: %ld", (long)pthread_self());

    if (m_unity_texture_ptr == nullptr || m_vk_graphics == nullptr) {
        DEBUG("[Vulkan] Cannot copy to Unity texture: ptr=%p, graphics=%p", m_unity_texture_ptr, m_vk_graphics);
        return false;
    }

    // Ensure Unity is not in a render pass before we try to access a texture
    DEBUG_VERBOSE("[Vulkan] Calling EnsureOutsideRenderPass");
    m_vk_graphics->EnsureOutsideRenderPass();
    DEBUG_VERBOSE("[Vulkan] EnsureOutsideRenderPass complete");

    // Let Unity know we are about to modify the texture.
    // This returns the VkImage and transitions it to TRANSFER_DST_OPTIMAL for us.
    DEBUG_VERBOSE("[Vulkan] About to call AccessTexture");
    DEBUG_VERBOSE("[Vulkan]   Requested layout: VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL");
    DEBUG_VERBOSE("[Vulkan]   Requested stage: VK_PIPELINE_STAGE_TRANSFER_BIT");
    DEBUG_VERBOSE("[Vulkan]   Requested access: VK_ACCESS_TRANSFER_WRITE_BIT");

    UnityVulkanImage unityImage;
    memset(&unityImage, 0, sizeof(unityImage)); // Zero initialize

    bool accessResult = m_vk_graphics->AccessTexture(m_unity_texture_ptr,
        UnityVulkanWholeImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        kUnityVulkanResourceAccess_PipelineBarrier,
        &unityImage);

    DEBUG_VERBOSE("[Vulkan] AccessTexture returned: %s", accessResult ? "SUCCESS" : "FAILED");
    DEBUG_VERBOSE("[Vulkan]   Unity VkImage: %p", unityImage.image);
    DEBUG_VERBOSE("[Vulkan]   Unity layout: %d", unityImage.layout);
    DEBUG_VERBOSE("[Vulkan]   Unity tiling: %d", unityImage.tiling);
    DEBUG_VERBOSE("[Vulkan]   Unity usage: 0x%x", unityImage.usage);
    DEBUG_VERBOSE("[Vulkan]   Unity format: %d", unityImage.format);
    DEBUG_VERBOSE("[Vulkan]   Unity extent: %dx%dx%d", unityImage.extent.width, unityImage.extent.height, unityImage.extent.depth);

    if (!accessResult) {
        DEBUG("[Vulkan] Failed to access Unity texture - texture may be in use by Unity");
        return false;
    }

    if (unityImage.image == VK_NULL_HANDLE) {
        DEBUG("[Vulkan] ERROR: AccessTexture succeeded but returned VK_NULL_HANDLE for image!");
        return false;
    }

    // Get Unity's current command buffer so we can record our commands into it.
    DEBUG_VERBOSE("[Vulkan] Getting Unity command recording state");
    UnityVulkanRecordingState recordingState;
    memset(&recordingState, 0, sizeof(recordingState)); // Zero initialize

    bool recordingResult = m_vk_graphics->CommandRecordingState(&recordingState, kUnityVulkanGraphicsQueueAccess_DontCare);

    DEBUG_VERBOSE("[Vulkan] CommandRecordingState returned: %s", recordingResult ? "SUCCESS" : "FAILED");
    DEBUG_VERBOSE("[Vulkan]   Command buffer: %p", recordingState.commandBuffer);
    DEBUG_VERBOSE("[Vulkan]   Command buffer level: %d", recordingState.commandBufferLevel);

    if (!recordingResult) {
        DEBUG("[Vulkan] Failed to get command recording state");
        return false;
    }

    VkCommandBuffer cmdBuffer = recordingState.commandBuffer;
    if (cmdBuffer == VK_NULL_HANDLE) {
        DEBUG("[Vulkan] ERROR: Got null command buffer from Unity!");
        return false;
    }

    DEBUG_VERBOSE("[Vulkan] Using Unity command buffer: %p", cmdBuffer);


    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.image = buffer.vk_image_external;

    // Only transition on first frame (UNDEFINED -> TRANSFER_SRC_OPTIMAL)
    if (!m_vulkan_image_layout_initialized) {
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        m_vulkan_image_layout_initialized = true;
    }

    // Record the copy command
    DEBUG_VERBOSE("[Vulkan] Recording image copy command");
    DEBUG_VERBOSE("[Vulkan]   Source: %p (VLC external, layout: TRANSFER_SRC_OPTIMAL)", buffer.vk_image_external);
    DEBUG_VERBOSE("[Vulkan]   Destination: %p (Unity, layout: TRANSFER_DST_OPTIMAL)", unityImage.image);
    DEBUG_VERBOSE("[Vulkan]   Copy extent: %dx%dx1", width, height);

    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.dstOffset = { 0, 0, 0 };
    copyRegion.extent = { width, height, 1 };

    vkCmdCopyImage(cmdBuffer,
        buffer.vk_image_external, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        unityImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copyRegion);
    DEBUG_VERBOSE("[Vulkan] Image copy command recorded");


    // We do NOT submit the command buffer. Unity will do it.
    DEBUG_VERBOSE("[Vulkan] Copied to Unity texture by recording into Unity's command buffer");
    DEBUG_VERBOSE("[Vulkan] === copyToUnityTexture END (SUCCESS) ===");
    return true;
}

void* RenderAPI_Vulkan::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    (void)width; (void)height;

    DEBUG_VERBOSE("[Vulkan] === getVideoFrame called ===");
    DEBUG_VERBOSE("[Vulkan]   Current thread ID: %ld", (long)pthread_self());
    DEBUG_VERBOSE("[Vulkan]   Updated flag: %s", updated ? "TRUE" : "FALSE");
    DEBUG_VERBOSE("[Vulkan]   Unity texture ptr: %p", m_unity_texture_ptr);
    DEBUG_VERBOSE("[Vulkan]   idx_display: %zu, idx_swap: %zu, idx_render: %zu", idx_display, idx_swap, idx_render);

    std::lock_guard<std::mutex> lock(text_lock);

    if (out_updated)
        *out_updated = updated;

    // Check if buffers are initialized (resize was called)
    if (buffers[idx_display].vk_image_external == VK_NULL_HANDLE) {
        DEBUG("[Vulkan] Buffers not initialized yet, returning nullptr");
        return nullptr;
    }

    // Log buffer state
    DEBUG_VERBOSE("[Vulkan] Buffer states:");
    for (size_t i = 0; i < 3; i++) {
        DEBUG_VERBOSE("[Vulkan]   Buffer[%zu]: AHB=%p, VK_ext=%p, GL=%u",
              i, buffers[i].a_hardware_buffer, buffers[i].vk_image_external, buffers[i].gl_texture);
    }

    if (updated) {
        DEBUG_VERBOSE("[Vulkan] Frame updated");
        std::swap(idx_swap, idx_display);
        DEBUG_VERBOSE("[Vulkan] After swap: idx_display=%zu, idx_swap=%zu", idx_display, idx_swap);
        updated = false;

        // Schedule copy for render thread
        DEBUG_VERBOSE("[Vulkan] Scheduling texture copy for render thread");
        DEBUG_VERBOSE("[Vulkan]   Using buffer[%zu]", idx_display);
        DEBUG_VERBOSE("[Vulkan]   Buffer VK external image: %p", buffers[idx_display].vk_image_external);

        // Store which buffer to copy in the render event
        m_pending_copy_buffer_idx = idx_display;
        m_has_pending_copy = true;

        DEBUG_VERBOSE("[Vulkan] Copy scheduled, will be executed on render thread");
    }

    // Return Unity's texture pointer
    DEBUG_VERBOSE("[Vulkan] Returning Unity texture pointer: %p", m_unity_texture_ptr);
    return m_unity_texture_ptr;
}

void RenderAPI_Vulkan::releaseHardwareBufferResources()
{
    for (auto& buffer : buffers) {
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

#if VULKAN_ENABLE_VALIDATION
// Static storage for original function pointers
static PFN_vkGetInstanceProcAddr s_originalGetInstanceProcAddr = nullptr;
static PFN_vkCreateInstance s_originalCreateInstance = nullptr;

// Wrapped vkCreateInstance that adds validation layers
static VKAPI_ATTR VkResult VKAPI_CALL Wrapped_vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    DEBUG("[Vulkan] Intercepted vkCreateInstance");

    // Modify the create info to add validation layer
    VkInstanceCreateInfo modifiedCreateInfo = *pCreateInfo;
    std::vector<const char*> enabledLayers;

    // Copy existing layers
    for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount; i++) {
        enabledLayers.push_back(pCreateInfo->ppEnabledLayerNames[i]);
    }

    // Add validation layer if not already present
    bool hasValidation = false;
    for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount; i++) {
        if (strcmp(pCreateInfo->ppEnabledLayerNames[i], "VK_LAYER_KHRONOS_validation") == 0) {
            hasValidation = true;
            break;
        }
    }

    if (!hasValidation) {
        DEBUG("[Vulkan] Adding VK_LAYER_KHRONOS_validation to instance layers");
        enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
    }

    // Add debug utils extension if not present
    std::vector<const char*> enabledExtensions;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        enabledExtensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
    }

    bool hasDebugUtils = false;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
            hasDebugUtils = true;
            break;
        }
    }

    if (!hasDebugUtils) {
        DEBUG("[Vulkan] Adding VK_EXT_debug_utils to instance extensions");
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    modifiedCreateInfo.enabledLayerCount = enabledLayers.size();
    modifiedCreateInfo.ppEnabledLayerNames = enabledLayers.data();
    modifiedCreateInfo.enabledExtensionCount = enabledExtensions.size();
    modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();

    DEBUG("[Vulkan] Creating instance with %u layers and %u extensions",
          modifiedCreateInfo.enabledLayerCount, modifiedCreateInfo.enabledExtensionCount);

    // Call the original vkCreateInstance
    VkResult result = s_originalCreateInstance(&modifiedCreateInfo, pAllocator, pInstance);

    if (result == VK_SUCCESS) {
        DEBUG("[Vulkan] Instance created successfully with validation layers");
    } else {
        DEBUG("[Vulkan] Instance creation failed with result: %d", result);
    }

    return result;
}

// Wrapped vkGetInstanceProcAddr that intercepts vkCreateInstance
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL Wrapped_vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName)
{
    if (strcmp(pName, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)Wrapped_vkCreateInstance;
    }
    return (PFN_vkVoidFunction)s_originalGetInstanceProcAddr(instance, pName);
}

// Intercept callback to enable validation layers
static PFN_vkGetInstanceProcAddr UNITY_INTERFACE_API VulkanInitCallback(
    PFN_vkGetInstanceProcAddr getInstanceProcAddr, void* userdata)
{
    (void)userdata;  // Unused
    DEBUG("[Vulkan] VulkanInitCallback invoked - intercepting instance creation");

    // Store the original vkGetInstanceProcAddr
    s_originalGetInstanceProcAddr = getInstanceProcAddr;

    // Get original vkCreateInstance
    s_originalCreateInstance = (PFN_vkCreateInstance)getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (!s_originalCreateInstance) {
        DEBUG("[Vulkan] Failed to get vkCreateInstance");
        return getInstanceProcAddr;
    }

    DEBUG("[Vulkan] Successfully intercepted vkGetInstanceProcAddr and vkCreateInstance");

    // Return our wrapped function
    return Wrapped_vkGetInstanceProcAddr;
}
#endif // VULKAN_ENABLE_VALIDATION

void RenderAPI_Vulkan::InitializeValidationLayers(IUnityInterfaces* interfaces)
{
    DEBUG("[Vulkan] InitializeValidationLayers called");
    DEBUG("[Vulkan] Validation layers are disabled (VULKAN_ENABLE_VALIDATION=0)");
    (void)interfaces; // Suppress unused parameter warning
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

        // Log Vulkan API version and device info
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(m_vk_instance.physicalDevice, &deviceProperties);
        DEBUG("[Vulkan] API %d.%d.%d - %s (Vendor 0x%X)",
            VK_VERSION_MAJOR(deviceProperties.apiVersion),
            VK_VERSION_MINOR(deviceProperties.apiVersion),
            VK_VERSION_PATCH(deviceProperties.apiVersion),
            deviceProperties.deviceName,
            deviceProperties.vendorID);
#if VULKAN_ENABLE_VALIDATION
        // Setup debug messenger if validation layers are enabled
        if (s_debug_messenger == VK_NULL_HANDLE) {
            vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(m_vk_instance.instance, "vkCreateDebugUtilsMessengerEXT");
            vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(m_vk_instance.instance, "vkDestroyDebugUtilsMessengerEXT");

            if (vkCreateDebugUtilsMessengerEXT) {
                VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
                debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                debugInfo.messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                debugInfo.messageType =
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                debugInfo.pfnUserCallback = DebugCallback;

                VkResult result = vkCreateDebugUtilsMessengerEXT(
                    m_vk_instance.instance, &debugInfo, nullptr, &s_debug_messenger);

                if (result == VK_SUCCESS) {
                    DEBUG("[Vulkan] Debug messenger created successfully");
                } else {
                    DEBUG("[Vulkan] Failed to create debug messenger: %d", result);
                }
            } else {
                DEBUG("[Vulkan] Debug utils extension not available");
            }
        }
#endif // VULKAN_ENABLE_VALIDATION

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

        DEBUG("[Vulkan] kUnityGfxDeviceEventInitialize success");
    }
    else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[Vulkan] kUnityGfxDeviceEventShutdown");
        releaseHardwareBufferResources();

        if (m_context != EGL_NO_CONTEXT) eglDestroyContext(m_display, m_context);
        if (m_surface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_surface);
        if (m_display != EGL_NO_DISPLAY) eglTerminate(m_display);
    }
}

void RenderAPI_Vulkan::onRenderEvent()
{
    DEBUG_VERBOSE("[Vulkan] === onRenderEvent: Entry ===");
    DEBUG_VERBOSE("[Vulkan]   Current thread ID: %ld", (long)pthread_self());
    DEBUG_VERBOSE("[Vulkan]   m_has_pending_copy: %s", m_has_pending_copy ? "TRUE" : "FALSE");

    std::lock_guard<std::mutex> lock(text_lock);

    if (!m_has_pending_copy) {
        DEBUG_VERBOSE("[Vulkan] === onRenderEvent: No pending copy, returning ===");
        return; // Nothing to do
    }

    DEBUG_VERBOSE("[Vulkan] === onRenderEvent: Processing pending copy ===");
    DEBUG_VERBOSE("[Vulkan]   Buffer index: %zu", m_pending_copy_buffer_idx);
    DEBUG_VERBOSE("[Vulkan]   Unity texture ptr: %p", m_unity_texture_ptr);
    DEBUG_VERBOSE("[Vulkan]   VK graphics interface: %p", m_vk_graphics);

    bool copySuccess = copyToUnityTexture(buffers[m_pending_copy_buffer_idx]);

    if (copySuccess) {
        DEBUG_VERBOSE("[Vulkan] Render event: Copy succeeded");
    } else {
        DEBUG("[Vulkan] Render event: Copy failed, will retry next frame");
    }

    m_has_pending_copy = false;
    DEBUG_VERBOSE("[Vulkan] === onRenderEvent: Complete ===");
}

#if VULKAN_ENABLE_VALIDATION
// External function called from RenderingPlugin.cpp to initialize validation
extern "C" void InitializeVulkanValidation(IUnityInterfaces* interfaces)
{
    RenderAPI_Vulkan::InitializeValidationLayers(interfaces);
}
#else
// Stub function when validation is disabled
extern "C" void InitializeVulkanValidation(IUnityInterfaces* interfaces)
{
    (void)interfaces; // Suppress unused parameter warning
}
#endif

#endif // UNITY_ANDROID
