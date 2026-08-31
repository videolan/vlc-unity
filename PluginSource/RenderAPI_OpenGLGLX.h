#pragma once

#include "LinuxDMABufProducer.h"
#include "LinuxOpenGLUnityImports.h"
#include "RenderAPI_OpenGLBase.h"

#include <GL/glx.h>
#include <X11/Xlib.h>
#include <memory>

class RenderAPI_OpenGLGLX final : public RenderAPI_OpenGLBase,
                                  public ILinuxDMABufProducerContext,
                                  public LinuxOpenGLUnityImportManager
{
public:
    explicit RenderAPI_OpenGLGLX(UnityGfxRenderer apiType);
    ~RenderAPI_OpenGLGLX() override;

    void setVlcContext(libvlc_media_player_t* mp) override;
    void unsetVlcContext(libvlc_media_player_t* mp) override;
    void ProcessDeviceEvent(UnityGfxDeviceEventType type,
                            IUnityInterfaces* interfaces) override;
    void retrieveOpenGLContext() override;
    void ensureCurrentContext() override;
    bool makeCurrent(bool current) override;
    void performRenderThreadWork() override;
    void* getVideoFrame(unsigned width, unsigned height,
                        bool* outUpdated) override;
    bool isInitialized() const override
    {
        const bool contextReady = m_context && m_pbuffer != None;
        return contextReady && (m_sharedContext || m_producer != nullptr);
    }
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

    static void* get_proc_address(void*, const char* name);
    bool producerMakeCurrent(bool current) override { return makeCurrent(current); }
    void* producerLoadProc(const char* name) override
    {
        return get_proc_address(nullptr, name);
    }

private:
    bool hasRenderThreadContext() const override;
    LinuxOpenGLContextIdentity currentRenderThreadContextIdentity()
        const override
    {
        return {
            reinterpret_cast<uintptr_t>(glXGetCurrentContext()), false
        };
    }

    bool createPrivateContext();
    bool verifySharedContext();
    bool initializeDMABuf();
    bool tryDMABufDevice(const std::string& path);
    void shutdownInternal();
    void prepareFrameForPublication() override;
    void releaseFrameSynchronization() override;
    void waitForSharedFrame();
    static bool staticMakeCurrent(void* data, bool current);
    static void sharedSwap(void* opaque);

    Display* m_display = nullptr;
    GLXPbuffer m_pbuffer = None;
    GLXContext m_context = nullptr;
    GLXFBConfig m_chosenConfig = nullptr;
    libvlc_media_player_t* m_pendingPlayer = nullptr;
    bool m_contextCreationReportedSharing = false;
    bool m_sharedContext = false;
    std::mutex m_sharedFrameMutex;
    GLsync m_sharedFrameFence = nullptr;
    LinuxGBMDevice m_gbm;
    std::unique_ptr<LinuxDMABufProducer> m_producer;

    static GLXContext s_unityContext;
    static Display* s_unityDisplay;
    static GLuint s_unityProbeTexture;
};
