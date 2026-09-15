#pragma once

#include "LinuxDMABufProducer.h"

#include <GL/glx.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

class OpenGLWatermark;

struct LinuxOpenGLContextIdentity
{
    uintptr_t handle = 0;
    bool isEgl = false;
};

// Restores whichever Unity OpenGL context was current after a backend briefly
// activates and destroys its private producer context.
class ScopedLinuxOpenGLContextRestore
{
public:
    ScopedLinuxOpenGLContextRestore(GLXContext privateGlx,
                                    EGLContext privateEgl)
        : m_privateGlx(privateGlx), m_privateEgl(privateEgl),
          m_eglApi(eglQueryAPI()),
          m_eglDisplay(eglGetCurrentDisplay()),
          m_eglContext(eglGetCurrentContext()),
          m_eglDraw(eglGetCurrentSurface(EGL_DRAW)),
          m_eglRead(eglGetCurrentSurface(EGL_READ)),
          m_glxDisplay(glXGetCurrentDisplay()),
          m_glxContext(glXGetCurrentContext()),
          m_glxDraw(glXGetCurrentDrawable()),
          m_glxRead(glXGetCurrentReadDrawable())
    {
    }

    ~ScopedLinuxOpenGLContextRestore()
    {
        // An initialization attempt may have bound a different client API,
        // even when no EGL context was current on entry (GLX or main thread).
        eglBindAPI(m_eglApi);
        if (m_eglContext != EGL_NO_CONTEXT && m_eglContext != m_privateEgl) {
            eglMakeCurrent(m_eglDisplay, m_eglDraw, m_eglRead, m_eglContext);
        } else if (m_glxContext && m_glxContext != m_privateGlx &&
                   m_glxDisplay) {
            glXMakeContextCurrent(
                m_glxDisplay, m_glxDraw, m_glxRead, m_glxContext);
        }
    }

    GLXContext glxContext() const { return m_glxContext; }

    bool releaseCurrent()
    {
        // GLVND rejects switching from a live GLX context directly to EGL.
        // Unbind only the calling thread's context, then restore on every exit.
        if (m_eglContext != EGL_NO_CONTEXT)
            return eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE,
                                  EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE;
        if (m_glxContext && m_glxDisplay)
            return glXMakeContextCurrent(m_glxDisplay, None, None, nullptr) == True;
        return true;
    }

private:
    GLXContext m_privateGlx;
    EGLContext m_privateEgl;
    EGLenum m_eglApi;
    EGLDisplay m_eglDisplay;
    EGLContext m_eglContext;
    EGLSurface m_eglDraw;
    EGLSurface m_eglRead;
    Display* m_glxDisplay;
    GLXContext m_glxContext;
    GLXDrawable m_glxDraw;
    GLXDrawable m_glxRead;
};

// Shared DMA-BUF to Unity texture-import machinery for the Linux OpenGL
// backends (GLX and desktop EGL): watermark hooks, slot invalidation and
// the EXT_memory_object imports consumed by Unity's renderer.
class LinuxOpenGLUnityImportManager : public ILinuxDMABufProducerObserver
{
public:
    LinuxOpenGLUnityImportManager(const char* logPrefix,
                                  ILinuxDMABufProducerContext& context,
                                  OpenGLWatermark* watermark = nullptr);
    ~LinuxOpenGLUnityImportManager() override = default;

    void attach(LinuxDMABufProducer* producer) { m_attachedProducer = producer; }

    void beginShutdown();
    bool finishShutdownOnRenderThread();
    bool canDestroy() const { return m_shutdownComplete; }
    void prepareImportsForPluginUnload();

    void abandonImports();
    void refresh();
    void* videoFrame(bool* outUpdated);

protected:
    virtual bool hasRenderThreadContext() const = 0;
    virtual LinuxOpenGLContextIdentity currentRenderThreadContextIdentity()
        const = 0;

    bool onProducerSetup() override;
    void onProducerCleanup() override;
    bool onBeforeProducerSwap(GLuint framebuffer,
                              unsigned width, unsigned height) override;
    bool onDMABufSlotCreated(size_t index,
                             const LinuxDMABufSlot& slot) override;
    void onDMABufSlotDestroying(size_t index,
                                const LinuxDMABufSlot& slot) override;

private:
    struct UnityImport
    {
        GLuint memoryObject = 0;
        GLuint texture = 0;
    };

    bool importSlotToUnity(size_t index);
    void destroyImportsLocked(
        std::array<UnityImport, LinuxDMABufProducer::SlotCount>& imports);
    void abandonCurrentImportsLocked();

    const char* m_logPrefix;
    ILinuxDMABufProducerContext& m_producerContext;
    LinuxDMABufProducer* m_attachedProducer = nullptr;
    OpenGLWatermark* m_watermark = nullptr;
    std::array<UnityImport, LinuxDMABufProducer::SlotCount> m_imports;
    std::mutex m_mutex;
    std::atomic<bool> m_imported { false };
    // Renderer lifecycle calls are serialized by RenderAPIEntry::callMutex.
    bool m_shutdownRequested = false;
    bool m_shutdownComplete = false;
    LinuxOpenGLContextIdentity m_importContext;
};
