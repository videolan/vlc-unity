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
#include "TrialWatermark.h"
#include "VulkanPlatformRequirements.h"

#include <android/hardware_buffer_jni.h>
#include <jni.h>
#include <stdexcept>
#include <utility>
#include <vlc/libvlc_media_player.h>

extern JNIEnv* jni_env;

#if VULKAN_ENABLE_VALIDATION
VkDebugUtilsMessengerEXT RenderAPI_Vulkan::s_debug_messenger = VK_NULL_HANDLE;
PFN_vkCreateDebugUtilsMessengerEXT
    RenderAPI_Vulkan::vkCreateDebugUtilsMessengerEXT = nullptr;
PFN_vkDestroyDebugUtilsMessengerEXT
    RenderAPI_Vulkan::vkDestroyDebugUtilsMessengerEXT = nullptr;
#endif

namespace {

#if VULKAN_ENABLE_VALIDATION
VKAPI_ATTR VkBool32 VKAPI_CALL validationDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*)
{
    const char* level = "VERBOSE";
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        level = "ERROR";
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        level = "WARNING";
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        level = "INFO";
    DEBUG("[VALIDATION][%s] %s", level,
          callbackData && callbackData->pMessage
              ? callbackData->pMessage : "(no message)");
    return VK_FALSE;
}
#endif

bool staticMakeCurrent(void* data, bool current)
{
    return static_cast<RenderAPI_Vulkan*>(data)->makeCurrent(current);
}

void staticSwap(void* data)
{
    RenderAPI_Vulkan::swap(data);
}

} // namespace

RenderAPI* CreateRenderAPI_Vulkan(UnityGfxRenderer apiType)
{
    return new RenderAPI_Vulkan(apiType);
}

RenderAPI_Vulkan::RenderAPI_Vulkan(UnityGfxRenderer apiType)
    : RenderAPI_OpenEGL(apiType)
{
    DEBUG("[Vulkan-Android] renderer created");
}

RenderAPI_Vulkan::~RenderAPI_Vulkan()
{
    shutdownRenderer();
    if (m_awindow)
        destroyWindowSurface(m_awindow);
}

#if VULKAN_ENABLE_VALIDATION
void RenderAPI_Vulkan::initializeValidationMessenger()
{
    if (s_debug_messenger != VK_NULL_HANDLE ||
        m_vkInstance.instance == VK_NULL_HANDLE) {
        return;
    }
    vkCreateDebugUtilsMessengerEXT =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(
                m_vkInstance.instance, "vkCreateDebugUtilsMessengerEXT"));
    vkDestroyDebugUtilsMessengerEXT =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(
                m_vkInstance.instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (!vkCreateDebugUtilsMessengerEXT || !vkDestroyDebugUtilsMessengerEXT) {
        DEBUG("[Vulkan-Android] optional validation debug utils are unavailable");
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = validationDebugCallback;
    const VkResult result = vkCreateDebugUtilsMessengerEXT(
        m_vkInstance.instance, &info, nullptr, &s_debug_messenger);
    if (result != VK_SUCCESS) {
        DEBUG("[Vulkan-Android] optional debug messenger creation failed: %d",
              static_cast<int>(result));
    }
}

void RenderAPI_Vulkan::shutdownValidationMessenger()
{
    if (s_debug_messenger != VK_NULL_HANDLE &&
        vkDestroyDebugUtilsMessengerEXT &&
        m_vkInstance.instance != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(
            m_vkInstance.instance, s_debug_messenger, nullptr);
    }
    s_debug_messenger = VK_NULL_HANDLE;
    vkCreateDebugUtilsMessengerEXT = nullptr;
    vkDestroyDebugUtilsMessengerEXT = nullptr;
}
#endif

jobject RenderAPI_Vulkan::createWindowSurface()
{
    jclass activityThread = jni_env->FindClass("android/app/ActivityThread");
    jmethodID currentApplication = jni_env->GetStaticMethodID(
        activityThread, "currentApplication", "()Landroid/app/Application;");
    jobject app = jni_env->CallStaticObjectMethod(activityThread, currentApplication);

    jclass contextClass = jni_env->FindClass("android/content/Context");
    jmethodID getClassLoader = jni_env->GetMethodID(
        contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject classLoader = jni_env->CallObjectMethod(app, getClassLoader);
    jclass classLoaderClass = jni_env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = jni_env->GetMethodID(
        classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring className = jni_env->NewStringUTF("org.videolan.libvlc.AWindow");
    jclass windowClass = static_cast<jclass>(
        jni_env->CallObjectMethod(classLoader, loadClass, className));
    jni_env->DeleteLocalRef(className);
    if (!windowClass) {
        DEBUG("[Vulkan-Android] org.videolan.libvlc.AWindow is unavailable");
        return nullptr;
    }

    jmethodID constructor = jni_env->GetMethodID(
        windowClass, "<init>", "(Lorg/videolan/libvlc/AWindow$SurfaceCallback;)V");
    jobject window = jni_env->NewObject(windowClass, constructor, nullptr);
    return jni_env->NewGlobalRef(window);
}

void RenderAPI_Vulkan::destroyWindowSurface(jobject object)
{
    if (object)
        jni_env->DeleteGlobalRef(object);
}

void RenderAPI_Vulkan::setVlcContext(libvlc_media_player_t* mp)
{
    m_mp = mp;
    if (!m_initialized.load()) {
        m_pendingPlayer = mp;
        return;
    }
    m_pendingPlayer = nullptr;
    if (!m_awindow)
        m_awindow = createWindowSurface();
    if (m_awindow)
        libvlc_media_player_set_android_context(mp, m_awindow);

    libvlc_video_set_output_callbacks(
        mp, libvlc_video_engine_gles2, setup, cleanup, nullptr, resize,
        staticSwap, staticMakeCurrent, RenderAPI_OpenEGL::get_proc_address,
        nullptr, nullptr, this);
}

void RenderAPI_Vulkan::unsetVlcContext(libvlc_media_player_t* mp)
{
    if (!mp)
        return;
    if (m_pendingPlayer == mp) {
        m_pendingPlayer = nullptr;
    } else {
        libvlc_video_set_output_callbacks(
            mp, libvlc_video_engine_disable, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    }
    if (m_mp == mp)
        m_mp = nullptr;
}

bool RenderAPI_Vulkan::setup(void** opaque,
                             const libvlc_video_setup_device_cfg_t* cfg,
                             libvlc_video_setup_device_info_t* out)
{
    (void)cfg;
    (void)out;
    if (!opaque || !*opaque)
        return false;
    auto* that = static_cast<RenderAPI_Vulkan*>(*opaque);
    that->width = 0;
    that->height = 0;
#if defined(SHOW_WATERMARK)
    if (!that->makeCurrent(true))
        return false;
    const bool result = that->watermark.setup();
    that->makeCurrent(false);
    return result;
#else
    return true;
#endif
}

void RenderAPI_Vulkan::cleanup(void* opaque)
{
    auto* that = static_cast<RenderAPI_Vulkan*>(opaque);
    if (!that || !that->makeCurrent(true))
        return;
    {
        std::lock_guard<std::mutex> lock(that->text_lock);
        that->releaseHardwareBufferResourcesLocked();
    }
#if defined(SHOW_WATERMARK)
    that->watermark.cleanup();
#endif
    that->makeCurrent(false);
}

bool RenderAPI_Vulkan::resize(void* opaque,
                              const libvlc_video_render_cfg_t* cfg,
                              libvlc_video_output_cfg_t* output)
{
    auto* that = static_cast<RenderAPI_Vulkan*>(opaque);
    if (!that || !cfg || !output || !that->makeCurrent(true))
        return false;
    std::lock_guard<std::mutex> lock(that->text_lock);

    if (cfg->width != that->width || cfg->height != that->height) {
        that->releaseHardwareBufferResourcesLocked();
        that->width = cfg->width;
        that->height = cfg->height;
        try {
            for (size_t i = 0; i < AndroidVulkanFrameSource::SlotCount; ++i) {
                that->createHardwareBuffer(
                    i, cfg->width, cfg->height, that->buffers[i]);
            }
        } catch (const std::exception& error) {
            DEBUG("[Vulkan-Android] buffer creation failed: %s", error.what());
            that->releaseHardwareBufferResourcesLocked();
            that->makeCurrent(false);
            return false;
        }
    }

    that->idx_render = 0;
    that->idx_swap = 1;
    that->idx_display = 2;
    that->updated = false;
    glBindFramebuffer(GL_FRAMEBUFFER, that->buffers[that->idx_render].fbo);

    output->u.opengl_format = GL_RGBA;
    output->full_range = true;
    output->colorspace = libvlc_video_colorspace_BT709;
    output->primaries = libvlc_video_primaries_BT709;
    output->transfer = libvlc_video_transfer_func_SRGB;
    output->orientation = libvlc_video_orient_bottom_right;
    return true;
}

void RenderAPI_Vulkan::swap(void* opaque)
{
    auto* that = static_cast<RenderAPI_Vulkan*>(opaque);
    if (!that || that->m_stopping.load() || !that->m_initialized.load())
        return;
    std::lock_guard<std::mutex> lock(that->text_lock);
    if (that->updated ||
        !that->m_frameSource.availableForProducer(that->idx_swap)) {
        return;
    }
#if defined(SHOW_WATERMARK)
    if (!libvlc_unity_trial_allows_frame())
        return;
    that->watermark.draw(that->buffers[that->idx_render].fbo,
                         that->width, that->height);
#endif
    RenderAPIHardwareBuffer& rendered = that->buffers[that->idx_render];
    if (rendered.producer_sync != EGL_NO_SYNC_KHR)
        that->eglDestroySyncKHR(that->m_display, rendered.producer_sync);
    rendered.producer_sync = that->eglCreateSyncKHR(
        that->m_display, EGL_SYNC_FENCE_KHR, nullptr);
    if (rendered.producer_sync == EGL_NO_SYNC_KHR) {
        that->m_producerSyncFailed.store(true);
        DEBUG("[Vulkan-Android] failed to create GL producer completion fence");
        return;
    }
    glFlush();
    that->updated = true;
    std::swap(that->idx_swap, that->idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, that->buffers[that->idx_render].fbo);
}

void RenderAPI_Vulkan::createHardwareBuffer(
    size_t index, unsigned bufferWidth, unsigned bufferHeight,
    RenderAPIHardwareBuffer& buffer)
{
    buffer.clearHandles();
    AHardwareBuffer_Desc description = {};
    description.width = bufferWidth;
    description.height = bufferHeight;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                        AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    if (AHardwareBuffer_allocate(
            &description, &buffer.a_hardware_buffer) != 0) {
        throw std::runtime_error("AHardwareBuffer allocation failed");
    }

    EGLClientBuffer clientBuffer =
        eglGetNativeClientBufferANDROID(buffer.a_hardware_buffer);
    const EGLint attributes[] = {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE
    };
    buffer.egl_image = eglCreateImageKHR(
        m_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
        clientBuffer, attributes);
    if (buffer.egl_image == EGL_NO_IMAGE_KHR) {
        AHardwareBuffer_release(buffer.a_hardware_buffer);
        buffer.a_hardware_buffer = nullptr;
        throw std::runtime_error("EGLImage creation failed");
    }

    glGenTextures(1, &buffer.gl_texture);
    glBindTexture(GL_TEXTURE_2D, buffer.gl_texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, buffer.egl_image);
    glGenFramebuffers(1, &buffer.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, buffer.gl_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        !m_frameSource.importHardwareBuffer(
            index, buffer.a_hardware_buffer, bufferWidth, bufferHeight)) {
        if (buffer.fbo)
            glDeleteFramebuffers(1, &buffer.fbo);
        if (buffer.gl_texture)
            glDeleteTextures(1, &buffer.gl_texture);
        eglDestroyImageKHR(m_display, buffer.egl_image);
        AHardwareBuffer_release(buffer.a_hardware_buffer);
        buffer.clearHandles();
        throw std::runtime_error("GL or Vulkan external image import failed");
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RenderAPI_Vulkan::setUnityTexture(void* unityTexturePtr)
{
    return m_initialized.load() && !m_stopping.load() &&
           m_copyCore.setUnityTexture(unityTexturePtr);
}

void* RenderAPI_Vulkan::getVideoFrame(
    unsigned requestedWidth, unsigned requestedHeight, bool* outUpdated)
{
    (void)requestedWidth;
    (void)requestedHeight;
    std::lock_guard<std::mutex> lock(text_lock);
    if (outUpdated)
        *outUpdated = false;
    if (!m_initialized.load() || m_stopping.load() ||
        m_producerSyncFailed.load())
        return nullptr;
    if (m_frameSource.image(idx_display) == VK_NULL_HANDLE)
        return nullptr;
    if (updated) {
        RenderAPIHardwareBuffer& ready = buffers[idx_swap];
        if (ready.producer_sync == EGL_NO_SYNC_KHR)
            return nullptr;
        const EGLint wait = eglClientWaitSyncKHR(
            m_display, ready.producer_sync, 0, 0);
        if (wait == EGL_TIMEOUT_EXPIRED_KHR)
            return nullptr;
        if (wait != EGL_CONDITION_SATISFIED_KHR) {
            m_producerSyncFailed.store(true);
            DEBUG("[Vulkan-Android] GL producer completion fence wait failed: 0x%x",
                  eglGetError());
            return nullptr;
        }
        eglDestroySyncKHR(m_display, ready.producer_sync);
        ready.producer_sync = EGL_NO_SYNC_KHR;
        std::swap(idx_swap, idx_display);
        updated = false;
        m_frameSource.publish(idx_display);
        if (outUpdated)
            *outUpdated = true;
    }
    // Managed code only uses this as a non-null updated-frame indication.
    return reinterpret_cast<void*>(static_cast<uintptr_t>(1));
}

void RenderAPI_Vulkan::releaseHardwareBufferResourcesLocked(
    bool deleteGlObjects)
{
    m_frameSource.releaseAllSlots();
    m_copyCore.pollCompletions();
    for (size_t i = 0; i < AndroidVulkanFrameSource::SlotCount; ++i) {
        RenderAPIHardwareBuffer& buffer = buffers[i];
        if (buffer.producer_sync != EGL_NO_SYNC_KHR && eglDestroySyncKHR)
            eglDestroySyncKHR(m_display, buffer.producer_sync);
        if (deleteGlObjects && buffer.fbo)
            glDeleteFramebuffers(1, &buffer.fbo);
        if (deleteGlObjects && buffer.gl_texture)
            glDeleteTextures(1, &buffer.gl_texture);
        if (buffer.egl_image != EGL_NO_IMAGE_KHR)
            eglDestroyImageKHR(m_display, buffer.egl_image);
        if (buffer.a_hardware_buffer)
            AHardwareBuffer_release(buffer.a_hardware_buffer);
        buffer.clearHandles();
    }
    width = 0;
    height = 0;
    updated = false;
}

void RenderAPI_Vulkan::ProcessDeviceEvent(
    UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    if (type == kUnityGfxDeviceEventInitialize) {
        if (m_initialized.load())
            return;
        m_stopping.store(false);
        m_producerSyncFailed.store(false);
        m_vkGraphics = interfaces ? interfaces->Get<IUnityGraphicsVulkan>() : nullptr;
        if (!m_vkGraphics) {
            DEBUG("[Vulkan-Android] IUnityGraphicsVulkan is unavailable");
            return;
        }
        if (!VulkanInterceptionWasRegistered() ||
            !VulkanInterceptionCreatedDevice()) {
            DEBUG("[Vulkan-Android] Vulkan bootstrap was not installed before device creation: %s",
                  VulkanInterceptionFailure());
        }
        m_vkInstance = m_vkGraphics->Instance();
#if VULKAN_ENABLE_VALIDATION
        initializeValidationMessenger();
#endif
        if (!m_frameSource.initialize(m_vkInstance) ||
            !m_submission.initialize(m_vkGraphics, m_vkInstance)) {
            DEBUG("[Vulkan-Android] frame source or completion tracking initialization failed");
            shutdownRenderer();
            return;
        }
        VulkanCopyCommandFunctions functions;
        functions.cmdPipelineBarrier = vkCmdPipelineBarrier;
        functions.cmdCopyImage = vkCmdCopyImage;
        if (!m_copyCore.initialize(
                m_vkGraphics, &m_frameSource, &m_submission, functions)) {
            shutdownRenderer();
            return;
        }

        eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
            eglGetProcAddress("eglCreateImageKHR"));
        eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            eglGetProcAddress("eglDestroyImageKHR"));
        eglCreateSyncKHR = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(
            eglGetProcAddress("eglCreateSyncKHR"));
        eglClientWaitSyncKHR = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
            eglGetProcAddress("eglClientWaitSyncKHR"));
        eglDestroySyncKHR = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
            eglGetProcAddress("eglDestroySyncKHR"));
        glEGLImageTargetTexture2DOES =
            reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
                eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        eglGetNativeClientBufferANDROID =
            reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
                eglGetProcAddress("eglGetNativeClientBufferANDROID"));
        if (!eglCreateImageKHR || !eglDestroyImageKHR || !eglCreateSyncKHR ||
            !eglClientWaitSyncKHR || !eglDestroySyncKHR ||
            !glEGLImageTargetTexture2DOES || !eglGetNativeClientBufferANDROID) {
            DEBUG("[Vulkan-Android] required EGL external-image or fence functions are unavailable");
            shutdownRenderer();
            return;
        }

        const EGLint configAttributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
        };
        const EGLint surfaceAttributes[] = {
            EGL_WIDTH, 2, EGL_HEIGHT, 2, EGL_NONE
        };
        EGLConfig config;
        EGLint configCount = 0;
        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_display == EGL_NO_DISPLAY ||
            !eglInitialize(m_display, nullptr, nullptr) ||
            !eglChooseConfig(m_display, configAttributes, &config, 1,
                             &configCount) || configCount == 0) {
            DEBUG("[Vulkan-Android] EGL display initialization failed: 0x%x",
                  eglGetError());
            shutdownRenderer();
            return;
        }
        m_surface = eglCreatePbufferSurface(
            m_display, config, surfaceAttributes);
        const EGLint contextAttributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
        };
        m_context = eglCreateContext(
            m_display, config, EGL_NO_CONTEXT, contextAttributes);
        if (m_surface == EGL_NO_SURFACE || m_context == EGL_NO_CONTEXT) {
            DEBUG("[Vulkan-Android] EGL producer context creation failed: 0x%x",
                  eglGetError());
            shutdownRenderer();
            return;
        }
        m_initialized = true;
        libvlc_media_player_t* pending = m_pendingPlayer;
        if (pending)
            setVlcContext(pending);
        DEBUG("[Vulkan-Android] initialization complete");
    } else if (type == kUnityGfxDeviceEventShutdown) {
        if (m_mp && m_pendingPlayer != m_mp) {
            libvlc_video_set_output_callbacks(
                m_mp, libvlc_video_engine_disable, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        m_pendingPlayer = m_mp;
        shutdownRenderer(true);
    }
}

void RenderAPI_Vulkan::performRenderThreadWork()
{
    std::lock_guard<std::mutex> lock(text_lock);
    if (!m_initialized.load())
        return;
    if (m_stopping.load())
        m_copyCore.pollCompletions();
    else
        m_copyCore.performRenderThreadWork();
}

void RenderAPI_Vulkan::beginShutdown()
{
    m_stopping.store(true);
    m_frameSource.discardReadyFrames();
}

bool RenderAPI_Vulkan::canDestroy() const
{
    return !m_initialized.load() || !m_copyCore.hasInFlightWork();
}

void RenderAPI_Vulkan::prepareForPluginUnload()
{
    beginShutdown();
    shutdownRenderer(true);
}

void RenderAPI_Vulkan::shutdownRenderer(bool abandonDeviceObjects)
{
    m_stopping.store(true);
    const bool haveContext = m_context != EGL_NO_CONTEXT &&
                             makeCurrent(true);
    {
        std::lock_guard<std::mutex> lock(text_lock);
        if (m_vkInstance.device != VK_NULL_HANDLE)
            // GL object names can only be deleted with their context current.
            // If makeCurrent failed, EGL/context teardown owns the objects and
            // we only forget their CPU-side names.
            releaseHardwareBufferResourcesLocked(haveContext);
        m_copyCore.pollCompletions();
        if (abandonDeviceObjects && m_submission.hasInFlightWork())
            m_submission.abandonForDeviceShutdown();
        m_copyCore.shutdown();
        m_submission.shutdown();
        m_frameSource.shutdown();
    }
#if VULKAN_ENABLE_VALIDATION
    shutdownValidationMessenger();
#endif
    if (haveContext)
        makeCurrent(false);
    if (m_context != EGL_NO_CONTEXT)
        eglDestroyContext(m_display, m_context);
    if (m_surface != EGL_NO_SURFACE)
        eglDestroySurface(m_display, m_surface);
    if (m_display != EGL_NO_DISPLAY)
        eglTerminate(m_display);
    m_context = EGL_NO_CONTEXT;
    m_surface = EGL_NO_SURFACE;
    m_display = EGL_NO_DISPLAY;
    m_vkGraphics = nullptr;
    m_vkInstance = {};
    eglGetNativeClientBufferANDROID = nullptr;
    eglCreateImageKHR = nullptr;
    eglDestroyImageKHR = nullptr;
    eglCreateSyncKHR = nullptr;
    eglClientWaitSyncKHR = nullptr;
    eglDestroySyncKHR = nullptr;
    glEGLImageTargetTexture2DOES = nullptr;
    m_initialized = false;
}

#endif // UNITY_ANDROID
