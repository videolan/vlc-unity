#pragma once

#include "LinuxDMABufProducer.h"
#include "LinuxOpenGLUnityImports.h"
#include "RenderAPI_OpenGLEGL.h"

#include <GL/glx.h>
#include <memory>

class RenderAPI_OpenGLLinuxEGL final : public RenderAPI_OpenEGL,
                                       public ILinuxDMABufProducerContext,
                                       public LinuxOpenGLUnityImportManager
{
public:
    explicit RenderAPI_OpenGLLinuxEGL(UnityGfxRenderer apiType);
    ~RenderAPI_OpenGLLinuxEGL() override;

    void setVlcContext(libvlc_media_player_t* mp) override;
    void unsetVlcContext(libvlc_media_player_t* mp) override;
    void ProcessDeviceEvent(UnityGfxDeviceEventType type,
                            IUnityInterfaces* interfaces) override;
    void retrieveOpenGLContext() override;
    void* getVideoFrame(unsigned width, unsigned height,
                        bool* outUpdated) override;
    void performRenderThreadWork() override;
    bool isInitialized() const override { return m_producer != nullptr; }
    void beginShutdown() override { LinuxOpenGLUnityImportManager::beginShutdown(); }
    void prepareForPluginUnload() override
    {
        LinuxOpenGLUnityImportManager::prepareImportsForPluginUnload();
    }
    bool canDestroy() const override
    {
        return LinuxOpenGLUnityImportManager::canDestroy();
    }
    bool retirementRequiresExplicitCleanupEvent() const override
    {
        return true;
    }

    static void* get_proc_address_desktop(void* data, const char* name);

    bool producerMakeCurrent(bool current) override { return makeCurrent(current); }
    void* producerLoadProc(const char* name) override
    {
        return get_proc_address_desktop(nullptr, name);
    }

private:
    bool hasRenderThreadContext() const override;
    LinuxOpenGLContextIdentity currentRenderThreadContextIdentity()
        const override
    {
        const EGLContext egl = eglGetCurrentContext();
        if (egl != EGL_NO_CONTEXT)
            return { reinterpret_cast<uintptr_t>(egl), true };
        return {
            reinterpret_cast<uintptr_t>(glXGetCurrentContext()), false
        };
    }

    bool initializeDrmAndContext();
    void releaseResources();

    LinuxGBMDevice m_gbm;
    std::unique_ptr<LinuxDMABufProducer> m_producer;
    libvlc_media_player_t* m_pendingPlayer = nullptr;
    static bool s_unityContextReady;
};
