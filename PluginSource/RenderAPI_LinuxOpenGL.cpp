#include "RenderAPI.h"
#include "LinuxGraphicsInterop.h"
#include "LinuxVideoOutput.h"
#include <GL/glx.h>
#include <EGL/egl.h>
#include <atomic>
#include <cstdlib>
#include <memory>

extern RenderAPI* CreateRenderAPI_OpenGLGLX(UnityGfxRenderer, LinuxVideoOutput*);
extern RenderAPI* CreateRenderAPI_OpenGLLinuxEGL(UnityGfxRenderer, LinuxVideoOutput*);

namespace {

std::atomic<LinuxOpenGLBackend> unityBinding{LinuxOpenGLBackend::Unknown};

class RenderAPI_LinuxOpenGL final : public RenderAPI
{
public:
    explicit RenderAPI_LinuxOpenGL(UnityGfxRenderer api) : m_api(api) {}

    void retrieveOpenGLContext() override
    {
        const auto binding = eglGetCurrentContext() != EGL_NO_CONTEXT ? LinuxOpenGLBackend::EGL :
            glXGetCurrentContext() ? LinuxOpenGLBackend::GLX : LinuxOpenGLBackend::Unknown;
        if (binding == LinuxOpenGLBackend::Unknown) return;
        createBackend(binding);
        if (m_backend) m_backend->retrieveOpenGLContext();
        // Publish after backend-specific capture so main-thread construction
        // cannot see a selected backend with an uninitialized shared context.
        unityBinding.store(binding, std::memory_order_release);
    }

    void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override
    {
        if (type == kUnityGfxDeviceEventInitialize) {
            const auto binding = unityBinding.load(std::memory_order_acquire);
            createBackend(binding);
            if (binding == LinuxOpenGLBackend::Unknown) return;
            if (m_backend) {
                m_backend->ProcessDeviceEvent(type, interfaces);
                if (m_player && m_backend->isInitialized() && !m_output.ready())
                    m_backend->setVlcContext(m_player);
            }
        } else {
            if (m_backend) m_backend->ProcessDeviceEvent(type, interfaces);
            if (type == kUnityGfxDeviceEventShutdown)
                unityBinding.store(LinuxOpenGLBackend::Unknown, std::memory_order_release);
        }
    }

    void setVlcContext(libvlc_media_player_t* mp) override
    {
        m_player = mp;
        if (!m_output.install(mp)) {
            DEBUG("[Linux] failed to install Unity video output callbacks");
            m_failed = true;
            m_output.cancel();
            return;
        }
        if (m_backend) m_backend->setVlcContext(mp);
    }
    void unsetVlcContext(libvlc_media_player_t* mp) override
    {
        m_output.cancel();
        if (m_backend) m_backend->unsetVlcContext(mp);
        libvlc_video_set_output_callbacks(mp, libvlc_video_engine_disable,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr);
        m_player = nullptr;
    }
    bool isInitialized() const override
    {
        return m_backend && m_backend->isInitialized() && (!m_player || m_output.ready());
    }
    bool hasVideoOutputFailure() const override { return m_failed; }
    void* getVideoFrame(unsigned h, unsigned w, bool* updated) override
    {
        if (m_backend) return m_backend->getVideoFrame(h, w, updated);
        if (updated) *updated = false;
        return nullptr;
    }
    void performRenderThreadWork() override
    {
        if (m_backend) m_backend->performRenderThreadWork();
    }
    void beginShutdown() override
    {
        m_output.cancel();
        if (m_backend) m_backend->beginShutdown();
    }
    void prepareForPluginUnload() override
    {
        m_output.cancel();
        if (m_backend) m_backend->prepareForPluginUnload();
    }
    bool canDestroy() const override { return !m_backend || m_backend->canDestroy(); }
    void setColorSpace(int colorSpace) override
    {
        m_colorSpace = colorSpace;
        if (m_backend) m_backend->setColorSpace(colorSpace);
    }

private:
    void createBackend(LinuxOpenGLBackend binding)
    {
        if (m_backend) return;
        const char* overrideValue = getenv("VLC_UNITY_LINUX_OPENGL_BACKEND");
        const auto backend = LinuxChooseOpenGLBackend(overrideValue, binding);
        if (backend == LinuxOpenGLBackend::Unknown) return;
        if (!LinuxIsOpenGLBackendOverrideValid(overrideValue))
            DEBUG("[Linux] ignoring invalid VLC_UNITY_LINUX_OPENGL_BACKEND=%s", overrideValue);
        DEBUG("[Linux] selected %s OpenGL interop backend (Unity binding=%s)",
              LinuxOpenGLBackendName(backend), LinuxOpenGLBackendName(binding));
        m_backend.reset(backend == LinuxOpenGLBackend::EGL ?
            CreateRenderAPI_OpenGLLinuxEGL(m_api, &m_output) :
            CreateRenderAPI_OpenGLGLX(m_api, &m_output));
        m_backend->setColorSpace(m_colorSpace);
    }

    UnityGfxRenderer m_api;
    LinuxVideoOutput m_output;
    std::unique_ptr<RenderAPI> m_backend;
    libvlc_media_player_t* m_player = nullptr;
    int m_colorSpace = 0;
    bool m_failed = false;
};
}

RenderAPI* CreateRenderAPI_LinuxOpenGL(UnityGfxRenderer api)
{
    return new RenderAPI_LinuxOpenGL(api);
}
