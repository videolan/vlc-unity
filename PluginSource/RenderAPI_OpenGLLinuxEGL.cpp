#include "RenderAPI_OpenGLLinuxEGL.h"
#include "LinuxGraphicsInterop.h"
#include "Log.h"

#include <cassert>
#include <cstdlib>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

bool RenderAPI_OpenGLLinuxEGL::s_unityContextReady = false;

RenderAPI* CreateRenderAPI_OpenGLLinuxEGL(UnityGfxRenderer apiType)
{
    return new RenderAPI_OpenGLLinuxEGL(apiType);
}

RenderAPI_OpenGLLinuxEGL::RenderAPI_OpenGLLinuxEGL(UnityGfxRenderer apiType)
    : RenderAPI_OpenEGL(apiType),
      LinuxOpenGLUnityImportManager(
          "EGL-Linux", *this
#if defined(SHOW_WATERMARK)
          , &watermark
#endif
      )
{
    m_display = EGL_NO_DISPLAY;
    m_surface = EGL_NO_SURFACE;
    m_context = EGL_NO_CONTEXT;
}

RenderAPI_OpenGLLinuxEGL::~RenderAPI_OpenGLLinuxEGL()
{
    releaseResources();
}

void* RenderAPI_OpenGLLinuxEGL::get_proc_address_desktop(
    void*, const char* name)
{
    void* function = reinterpret_cast<void*>(eglGetProcAddress(name));
    if (!function) {
        function = reinterpret_cast<void*>(glXGetProcAddressARB(
            reinterpret_cast<const GLubyte*>(name)));
    }
    return function;
}

void RenderAPI_OpenGLLinuxEGL::retrieveOpenGLContext()
{
    if (s_unityContextReady)
        return;
    s_unityContextReady = eglGetCurrentContext() != EGL_NO_CONTEXT ||
                          glXGetCurrentContext() != nullptr;
    if (s_unityContextReady)
        DEBUG("[EGL-Linux] Unity GL context is available");
}

bool RenderAPI_OpenGLLinuxEGL::initializeDrmAndContext()
{
    const char* overridePath = getenv("VLC_UNITY_DRM_DEVICE");
    const std::vector<std::string> candidates =
        LinuxBuildDrmDeviceCandidates("/dev/dri", overridePath);
    const std::string selected = LinuxSelectCompatibleDrmDevice(
        candidates, [this](const std::string& path) {
            return m_gbm.open("EGL-Linux", path);
        });
    if (selected.empty()) {
        DEBUG("[EGL-Linux] no DRM render node could create a GBM device");
        return false;
    }

    using GetPlatformDisplay = EGLDisplay (*)(EGLenum, void*, const EGLint*);
    auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplay>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay) {
        m_display = getPlatformDisplay(
            EGL_PLATFORM_GBM_KHR, m_gbm.get(), nullptr);
    }
    if (m_display == EGL_NO_DISPLAY) {
        m_display = eglGetDisplay(
            reinterpret_cast<EGLNativeDisplayType>(m_gbm.get()));
    }
    if (m_display == EGL_NO_DISPLAY ||
        !eglInitialize(m_display, nullptr, nullptr) ||
        !eglBindAPI(EGL_OPENGL_API)) {
        DEBUG("[EGL-Linux] GBM EGL display initialization failed: 0x%x",
              eglGetError());
        return false;
    }

    EGLConfig config = nullptr;
    EGLint count = 0;
    const EGLint attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, 0,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    if (!eglChooseConfig(m_display, attributes, &config, 1, &count) ||
        count == 0) {
        const EGLint fallback[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE
        };
        if (!eglChooseConfig(m_display, fallback, &config, 1, &count) ||
            count == 0) {
            DEBUG("[EGL-Linux] no desktop GL EGL config is available");
            return false;
        }
    }

    static const int versions[][2] = {{4, 5}, {3, 3}};
    for (const auto& version : versions) {
        const EGLint contextAttributes[] = {
            EGL_CONTEXT_MAJOR_VERSION, version[0],
            EGL_CONTEXT_MINOR_VERSION, version[1],
            EGL_CONTEXT_OPENGL_PROFILE_MASK,
            EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            EGL_NONE
        };
        m_context = eglCreateContext(
            m_display, config, EGL_NO_CONTEXT, contextAttributes);
        if (m_context != EGL_NO_CONTEXT)
            break;
    }
    m_surface = EGL_NO_SURFACE;
    if (m_context == EGL_NO_CONTEXT) {
        DEBUG("[EGL-Linux] desktop GL producer context creation failed: 0x%x",
              eglGetError());
        return false;
    }

    m_producer.reset(new LinuxDMABufProducer(
        "EGL-Linux", m_gbm, *this, this, nullptr, false));
    attach(m_producer.get());
    if (!m_producer->initialize()) {
        attach(nullptr);
        m_producer.reset();
        return false;
    }
    DEBUG("[EGL-Linux] selected DRM render node %s", selected.c_str());
    return true;
}

void RenderAPI_OpenGLLinuxEGL::ProcessDeviceEvent(
    UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    (void)interfaces;
    if (type == kUnityGfxDeviceEventInitialize) {
        if (m_producer || !s_unityContextReady)
            return;
        ScopedLinuxOpenGLContextRestore restoreContext(nullptr, m_context);
        if (!restoreContext.releaseCurrent()) {
            DEBUG("[EGL-Linux] could not release Unity context for producer initialization");
            return;
        }
        if (!initializeDrmAndContext()) {
            releaseResources();
            return;
        }
        libvlc_media_player_t* pending = m_pendingPlayer;
        m_pendingPlayer = nullptr;
        if (pending)
            setVlcContext(pending);
    } else if (type == kUnityGfxDeviceEventShutdown) {
        if (m_mp && m_pendingPlayer != m_mp && m_producer)
            m_producer->unsetVlcContext(m_mp);
        m_pendingPlayer = m_mp;
        releaseResources();
    }
}

void RenderAPI_OpenGLLinuxEGL::setVlcContext(libvlc_media_player_t* mp)
{
    m_mp = mp;
    if (!m_producer) {
        libvlc_media_player_t* previous = m_pendingPlayer;
        m_pendingPlayer = mp;
        assert((!previous || previous == mp) &&
               "second pending EGL Linux player");
        (void)previous;
        return;
    }
    m_pendingPlayer = nullptr;
    if (!m_producer->setVlcContext(mp))
        DEBUG("[EGL-Linux] failed to register shared DMA-BUF producer callbacks");
}

void RenderAPI_OpenGLLinuxEGL::unsetVlcContext(libvlc_media_player_t* mp)
{
    if (m_pendingPlayer == mp)
        m_pendingPlayer = nullptr;
    else if (m_producer)
        m_producer->unsetVlcContext(mp);
    m_mp = nullptr;
}

bool RenderAPI_OpenGLLinuxEGL::hasRenderThreadContext() const
{
    return glXGetCurrentContext() != nullptr ||
           eglGetCurrentContext() != EGL_NO_CONTEXT;
}

void RenderAPI_OpenGLLinuxEGL::performRenderThreadWork()
{
    if (finishShutdownOnRenderThread()) {
        releaseResources();
        return;
    }
    refresh();
}

void* RenderAPI_OpenGLLinuxEGL::getVideoFrame(
    unsigned, unsigned, bool* outUpdated)
{
    return videoFrame(outUpdated);
}

void RenderAPI_OpenGLLinuxEGL::releaseResources()
{
    {
        ScopedLinuxOpenGLContextRestore restoreContext(nullptr, m_context);
        abandonImports();
        if (m_producer) {
            if (!restoreContext.releaseCurrent()) {
                DEBUG("[EGL-Linux] could not release Unity context for producer cleanup");
                return;
            }
            m_producer->release();
            m_producer.reset();
        }
        attach(nullptr);
        if (m_context != EGL_NO_CONTEXT &&
            eglGetCurrentContext() == m_context) {
            eglMakeCurrent(
                m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
    }
    if (m_context != EGL_NO_CONTEXT)
        eglDestroyContext(m_display, m_context);
    if (m_surface != EGL_NO_SURFACE)
        eglDestroySurface(m_display, m_surface);
    if (m_display != EGL_NO_DISPLAY)
        eglTerminate(m_display);
    m_context = EGL_NO_CONTEXT;
    m_surface = EGL_NO_SURFACE;
    m_display = EGL_NO_DISPLAY;
    m_gbm.reset();
}
