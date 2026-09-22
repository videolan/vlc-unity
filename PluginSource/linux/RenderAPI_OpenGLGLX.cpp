#include "RenderAPI_OpenGLGLX.h"
#include "LinuxGraphicsInterop.h"
#include "Log.h"

#include <cassert>
#include <cstdlib>

#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

GLXContext RenderAPI_OpenGLGLX::s_unityContext = nullptr;
Display* RenderAPI_OpenGLGLX::s_unityDisplay = nullptr;
GLuint RenderAPI_OpenGLGLX::s_unityProbeTexture = 0;

namespace {

std::mutex errorHandlerMutex;

class GLXErrorTrap
{
public:
    using Handler = int (*)(Display*, XErrorEvent*);
    explicit GLXErrorTrap(Display* display)
        : m_display(display), m_lock(errorHandlerMutex)
    {
        XLockDisplay(display);
        XSync(display, False);
        s_active = this;
        m_previous = XSetErrorHandler(handle);
    }
    ~GLXErrorTrap()
    {
        XSync(m_display, False);
        XSetErrorHandler(m_previous);
        s_active = nullptr;
        XUnlockDisplay(m_display);
    }
    void begin()
    {
        XSync(m_display, False);
        m_error = 0;
    }
    int finish()
    {
        XSync(m_display, False);
        return m_error;
    }

private:
    static int handle(Display* display, XErrorEvent* event)
    {
        if (s_active && s_active->m_display == display) {
            s_active->m_error = event->error_code;
            return 0;
        }
        return s_active && s_active->m_previous
            ? s_active->m_previous(display, event) : 0;
    }
    static GLXErrorTrap* s_active;
    Display* m_display;
    Handler m_previous = nullptr;
    std::unique_lock<std::mutex> m_lock;
    int m_error = 0;
};

GLXErrorTrap* GLXErrorTrap::s_active = nullptr;

} // namespace

RenderAPI* CreateRenderAPI_OpenGLGLX(UnityGfxRenderer apiType, LinuxVideoOutput* output)
{
    return new RenderAPI_OpenGLGLX(apiType, output);
}

RenderAPI_OpenGLGLX::RenderAPI_OpenGLGLX(UnityGfxRenderer apiType, LinuxVideoOutput* output)
    : RenderAPI_OpenGLBase(apiType),
      LinuxOpenGLUnityImportManager(
          "GLX", *this
#if defined(SHOW_WATERMARK)
          , &watermark
#endif
      ), m_output(output)
{
}

RenderAPI_OpenGLGLX::~RenderAPI_OpenGLGLX()
{
    shutdownInternal();
}

bool RenderAPI_OpenGLGLX::makeCurrent(bool current)
{
    if (current) {
        if (!m_display || !m_context || m_pbuffer == None)
            return false;
        if (glXGetCurrentContext() == m_context)
            return true;
        return glXMakeContextCurrent(
            m_display, m_pbuffer, m_pbuffer, m_context) == True;
    }
    if (glXGetCurrentContext() != m_context)
        return true;
    return glXMakeContextCurrent(m_display, None, None, nullptr) == True;
}

bool RenderAPI_OpenGLGLX::staticMakeCurrent(void* data, bool current)
{
    return static_cast<RenderAPI_OpenGLGLX*>(data)->makeCurrent(current);
}

void* RenderAPI_OpenGLGLX::get_proc_address(void*, const char* name)
{
    return reinterpret_cast<void*>(glXGetProcAddressARB(
        reinterpret_cast<const GLubyte*>(name)));
}

void RenderAPI_OpenGLGLX::retrieveOpenGLContext()
{
    if (s_unityContext && s_unityProbeTexture)
        return;
    GLXContext current = glXGetCurrentContext();
    if (!current)
        return;
    if (!s_unityContext) {
        s_unityContext = current;
        s_unityDisplay = glXGetCurrentDisplay();
        DEBUG("[GLX] retrieved Unity context %p display %p vendor=%s renderer=%s version=%s",
              s_unityContext, s_unityDisplay,
              glGetString(GL_VENDOR) ? reinterpret_cast<const char*>(glGetString(GL_VENDOR)) : "?",
              glGetString(GL_RENDERER) ? reinterpret_cast<const char*>(glGetString(GL_RENDERER)) : "?",
              glGetString(GL_VERSION) ? reinterpret_cast<const char*>(glGetString(GL_VERSION)) : "?");
    }
    GLint previous = 0;
    clearGlErrors();
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glGenTextures(1, &s_unityProbeTexture);
    glBindTexture(GL_TEXTURE_2D, s_unityProbeTexture);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    glFinish();
    if (glGetError() != GL_NO_ERROR || !s_unityProbeTexture) {
        if (s_unityProbeTexture)
            glDeleteTextures(1, &s_unityProbeTexture);
        s_unityProbeTexture = 0;
    }
}

void RenderAPI_OpenGLGLX::ensureCurrentContext()
{
    if (!glXGetCurrentContext())
        makeCurrent(true);
}

bool RenderAPI_OpenGLGLX::createPrivateContext()
{
    m_display = s_unityDisplay;
    if (!m_display || !s_unityContext)
        return false;
    const int screen = DefaultScreen(m_display);
    static const int configAttributes[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, None
    };
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(
        m_display, screen, configAttributes, &count);
    if (!configs || count == 0)
        return false;

    int unityConfigId = 0;
    glXQueryContext(m_display, s_unityContext,
                    GLX_FBCONFIG_ID, &unityConfigId);
    for (int i = 1; i < count; ++i) {
        int id = 0;
        glXGetFBConfigAttrib(m_display, configs[i], GLX_FBCONFIG_ID, &id);
        if (id == unityConfigId) {
            std::swap(configs[0], configs[i]);
            break;
        }
    }

    using CreateContextAttribs = GLXContext (*)(
        Display*, GLXFBConfig, GLXContext, Bool, const int*);
    auto createContextAttribs = reinterpret_cast<CreateContextAttribs>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(
            "glXCreateContextAttribsARB")));
    const Bool direct = glXIsDirect(m_display, s_unityContext);
    GLXErrorTrap trap(m_display);
    for (int i = 0; i < count && !m_context; ++i) {
        if (createContextAttribs) {
            static const int versions[][2] = {{4, 5}, {3, 3}};
            for (const auto& version : versions) {
                const int attributes[] = {
                    GLX_CONTEXT_MAJOR_VERSION_ARB, version[0],
                    GLX_CONTEXT_MINOR_VERSION_ARB, version[1],
                    GLX_CONTEXT_PROFILE_MASK_ARB,
                    GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None
                };
                trap.begin();
                m_context = createContextAttribs(
                    m_display, configs[i], s_unityContext, direct, attributes);
                if (trap.finish() == 0 && m_context) {
                    m_chosenConfig = configs[i];
                    m_contextCreationReportedSharing = true;
                    break;
                }
                if (m_context)
                    glXDestroyContext(m_display, m_context);
                m_context = nullptr;
            }
        }
        if (!m_context) {
            trap.begin();
            m_context = glXCreateNewContext(
                m_display, configs[i], GLX_RGBA_TYPE, s_unityContext, direct);
            if (trap.finish() == 0 && m_context) {
                m_chosenConfig = configs[i];
                m_contextCreationReportedSharing = true;
            } else {
                if (m_context)
                    glXDestroyContext(m_display, m_context);
                m_context = nullptr;
            }
        }
    }
    if (!m_context) {
        m_chosenConfig = configs[0];
        trap.begin();
        m_context = glXCreateNewContext(
            m_display, m_chosenConfig, GLX_RGBA_TYPE, nullptr, direct);
        if (trap.finish() != 0) {
            if (m_context)
                glXDestroyContext(m_display, m_context);
            m_context = nullptr;
        }
    }
    if (m_context) {
        const int pbufferAttributes[] = {
            GLX_PBUFFER_WIDTH, 2, GLX_PBUFFER_HEIGHT, 2, None
        };
        m_pbuffer = glXCreatePbuffer(
            m_display, m_chosenConfig, pbufferAttributes);
        if (m_pbuffer == None) {
            glXDestroyContext(m_display, m_context);
            m_context = nullptr;
            m_chosenConfig = nullptr;
        }
    }
    XFree(configs);
    return m_context && m_pbuffer != None;
}

bool RenderAPI_OpenGLGLX::verifySharedContext()
{
    if (!s_unityProbeTexture)
        return false;
    const GLXContext previousContext = glXGetCurrentContext();
    const GLXDrawable previousDraw = glXGetCurrentDrawable();
    const GLXDrawable previousRead = glXGetCurrentReadDrawable();
    if (!makeCurrent(true))
        return false;
    clearGlErrors();
    const bool visible = glIsTexture(s_unityProbeTexture) == GL_TRUE;
    const GLenum error = glGetError();
    if (previousContext)
        glXMakeContextCurrent(
            m_display, previousDraw, previousRead, previousContext);
    else
        glXMakeContextCurrent(m_display, None, None, nullptr);
    return visible && error == GL_NO_ERROR;
}

bool RenderAPI_OpenGLGLX::tryDMABufDevice(const std::string& path)
{
    if (!m_gbm.open("GLX", path))
        return false;
    m_producer.reset(new LinuxDMABufProducer(
        "GLX", m_gbm, *this, this, nullptr, true));
    attach(m_producer.get());
    if (m_producer->initialize() && m_producer->probe())
        return true;
    attach(nullptr);
    m_producer.reset();
    m_gbm.reset();
    return false;
}

bool RenderAPI_OpenGLGLX::initializeDMABuf()
{
    GLXContext previousContext = glXGetCurrentContext();
    GLXDrawable previousDraw = glXGetCurrentDrawable();
    GLXDrawable previousRead = glXGetCurrentReadDrawable();
    if (!makeCurrent(true))
        return false;
    const char* overridePath = getenv("VLC_UNITY_DRM_DEVICE");
    const std::vector<std::string> candidates =
        LinuxBuildDrmDeviceCandidates("/dev/dri", overridePath);
    const std::string selected = LinuxSelectCompatibleDrmDevice(
        candidates, [this](const std::string& path) {
            return tryDMABufDevice(path);
        });
    glXMakeContextCurrent(
        m_display, previousDraw, previousRead, previousContext);
    if (selected.empty()) {
        DEBUG("[GLX] no DRM render node passed the GBM/GL import probe");
        return false;
    }
    DEBUG("[GLX] selected compatible DRM render node %s", selected.c_str());
    return true;
}

void RenderAPI_OpenGLGLX::ProcessDeviceEvent(
    UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    (void)interfaces;
    if (type == kUnityGfxDeviceEventInitialize) {
        if (isInitialized() || !createPrivateContext())
            return;
        const bool forceDMABuf = LinuxEnvironmentFlagEnabled(
            getenv("VLC_UNITY_GLX_FORCE_DMABUF"));
        m_sharedContext = !forceDMABuf &&
            m_contextCreationReportedSharing && verifySharedContext();
        if (!m_sharedContext && !initializeDMABuf()) {
            shutdownInternal();
            return;
        }
        DEBUG("[GLX] initialized: verified-direct-share=%d dma-buf=%d",
              m_sharedContext, m_producer != nullptr);
        libvlc_media_player_t* pending = m_pendingPlayer;
        m_pendingPlayer = nullptr;
        if (pending)
            setVlcContext(pending);
    } else if (type == kUnityGfxDeviceEventShutdown) {
        if (!m_output && m_mp && m_pendingPlayer != m_mp) {
            if (m_sharedContext) {
                libvlc_video_set_output_callbacks(
                    m_mp, libvlc_video_engine_disable, nullptr, nullptr,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    nullptr, nullptr);
            } else if (m_producer) {
                m_producer->unsetVlcContext(m_mp);
            }
        }
        m_pendingPlayer = m_mp;
        if (s_unityProbeTexture && glXGetCurrentContext() == s_unityContext)
            glDeleteTextures(1, &s_unityProbeTexture);
        s_unityProbeTexture = 0;
        s_unityContext = nullptr;
        s_unityDisplay = nullptr;
        shutdownInternal();
    }
}

void RenderAPI_OpenGLGLX::setVlcContext(libvlc_media_player_t* mp)
{
    m_mp = mp;
    if (!isInitialized()) {
        libvlc_media_player_t* previous = m_pendingPlayer;
        m_pendingPlayer = mp;
        assert((!previous || previous == mp) && "second pending GLX player");
        (void)previous;
        return;
    }
    m_pendingPlayer = nullptr;
    if (m_sharedContext) {
        if (m_output)
            m_output->configure({setup, cleanup, resize, sharedSwap,
                staticMakeCurrent, get_proc_address, this});
        else libvlc_video_set_output_callbacks(
            mp, libvlc_video_engine_opengl, setup, cleanup, nullptr, resize,
            sharedSwap, staticMakeCurrent, get_proc_address,
            nullptr, nullptr, this);
    } else if (!m_producer->setVlcContext(mp, m_output)) {
        DEBUG("[GLX] failed to register shared DMA-BUF producer callbacks");
    }
}

void RenderAPI_OpenGLGLX::unsetVlcContext(libvlc_media_player_t* mp)
{
    if (m_output) {
        m_pendingPlayer = nullptr;
        m_mp = nullptr;
        return; // The owner cancels the stable output callbacks.
    }
    if (m_pendingPlayer == mp)
        m_pendingPlayer = nullptr;
    else if (m_sharedContext) {
        libvlc_video_set_output_callbacks(
            mp, libvlc_video_engine_disable, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    } else if (m_producer) {
        m_producer->unsetVlcContext(mp);
    }
    m_mp = nullptr;
}

void RenderAPI_OpenGLGLX::sharedSwap(void* opaque)
{
    RenderAPI_OpenGLBase::swap(opaque);
}

void RenderAPI_OpenGLGLX::prepareFrameForPublication()
{
    GLsync next = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!next) {
        glFinish();
        return;
    }

    glFlush();
    std::lock_guard<std::mutex> lock(m_sharedFrameMutex);
    if (m_sharedFrameFence)
        glDeleteSync(m_sharedFrameFence);
    m_sharedFrameFence = next;
}

void RenderAPI_OpenGLGLX::releaseFrameSynchronization()
{
    std::lock_guard<std::mutex> lock(m_sharedFrameMutex);
    if (m_sharedFrameFence)
        glDeleteSync(m_sharedFrameFence);
    m_sharedFrameFence = nullptr;
}

void RenderAPI_OpenGLGLX::waitForSharedFrame()
{
    GLsync fence = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_sharedFrameMutex);
        fence = m_sharedFrameFence;
        m_sharedFrameFence = nullptr;
    }
    if (!fence)
        return;

    glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(fence);
}

bool RenderAPI_OpenGLGLX::hasRenderThreadContext() const
{
    return glXGetCurrentContext() != nullptr;
}

void RenderAPI_OpenGLGLX::performRenderThreadWork()
{
    if (finishShutdownOnRenderThread()) {
        shutdownInternal();
        return;
    }
    if (m_sharedContext) {
        if (hasRenderThreadContext())
            waitForSharedFrame();
        return;
    }
    refresh();
}

void* RenderAPI_OpenGLGLX::getVideoFrame(
    unsigned width, unsigned height, bool* outUpdated)
{
    if (m_sharedContext)
        return RenderAPI_OpenGLBase::getVideoFrame(width, height, outUpdated);
    return videoFrame(outUpdated);
}

void RenderAPI_OpenGLGLX::shutdownInternal()
{
    {
        ScopedLinuxOpenGLContextRestore restoreContext(
            m_context, EGL_NO_CONTEXT);
        const bool unityCurrent = s_unityContext &&
                                  glXGetCurrentContext() == s_unityContext;
        if (m_sharedContext &&
            (unityCurrent || restoreContext.glxContext() == m_context ||
             makeCurrent(true))) {
            releaseFrameSynchronization();
        }
        abandonImports();
        if (m_producer) {
            m_producer->release();
            m_producer.reset();
        }
        attach(nullptr);
        m_gbm.reset();
        if (m_display && m_context && glXGetCurrentContext() == m_context)
            glXMakeContextCurrent(m_display, None, None, nullptr);
    }
    if (m_context)
        glXDestroyContext(m_display, m_context);
    if (m_pbuffer != None)
        glXDestroyPbuffer(m_display, m_pbuffer);
    m_context = nullptr;
    m_pbuffer = None;
    m_display = nullptr;
    m_chosenConfig = nullptr;
    m_sharedContext = false;
    m_contextCreationReportedSharing = false;
}
