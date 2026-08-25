#pragma once

#include "LinuxDMABufProducer.h"

#include <array>
#include <atomic>
#include <mutex>

class OpenGLWatermark;

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
    void prepareImportsForPluginUnload();

    void abandonImports();
    void refresh();
    void* videoFrame(bool* outUpdated);

protected:
    virtual bool hasRenderThreadContext() const = 0;

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
    std::atomic<bool> m_shutdownRequested { false };
};
