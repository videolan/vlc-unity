#include "LinuxOpenGLUnityImports.h"
#include "LinuxDMABufWatermark.h"
#include "Log.h"
#include "LinuxGraphicsInterop.h"

#include <cstdint>

LinuxOpenGLUnityImportManager::LinuxOpenGLUnityImportManager(
    const char* logPrefix, ILinuxDMABufProducerContext& context,
    OpenGLWatermark* watermark)
    : m_logPrefix(logPrefix), m_producerContext(context),
      m_watermark(watermark)
{
}

bool LinuxOpenGLUnityImportManager::onProducerSetup()
{
    return LinuxDMABufWatermarkSetup(m_producerContext, m_watermark);
}

void LinuxOpenGLUnityImportManager::onProducerCleanup()
{
    LinuxDMABufWatermarkCleanup(m_producerContext, m_watermark);
}

bool LinuxOpenGLUnityImportManager::onBeforeProducerSwap(
    GLuint framebuffer, unsigned frameWidth, unsigned frameHeight)
{
    return LinuxDMABufWatermarkBeforeSwap(
        m_watermark, framebuffer, frameWidth, frameHeight);
}

bool LinuxOpenGLUnityImportManager::onDMABufSlotCreated(
    size_t, const LinuxDMABufSlot&)
{
    m_imported.store(false);
    return true;
}

void LinuxOpenGLUnityImportManager::onDMABufSlotDestroying(
    size_t, const LinuxDMABufSlot&)
{
    m_imported.store(false);
}

bool LinuxOpenGLUnityImportManager::importSlotToUnity(size_t index)
{
    const LinuxDMABufSlot& source = m_attachedProducer->slot(index);
    UnityImport& imported = m_imports[index];
    m_attachedProducer->rawGenTextures()(1, &imported.texture);
    m_attachedProducer->rawBindTexture()(GL_TEXTURE_2D, imported.texture);
    if (!LinuxGLImportMemoryFd(
            m_logPrefix, m_attachedProducer->createMemoryObjects(),
            m_attachedProducer->importMemoryFd(), m_attachedProducer->deleteMemoryObjects(),
            m_attachedProducer->memoryObjectParameter(),
            m_attachedProducer->textureStorageMemory(), imported.memoryObject,
            imported.texture, source.fd, source.size,
            m_attachedProducer->width(), m_attachedProducer->height(), "Unity")) {
        if (imported.texture)
            m_attachedProducer->rawDeleteTextures()(1, &imported.texture);
        imported = {};
        return false;
    }
    m_attachedProducer->rawTextureParameter()(
        GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_attachedProducer->rawTextureParameter()(
        GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_attachedProducer->rawTextureParameter()(
        GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_attachedProducer->rawTextureParameter()(
        GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_attachedProducer->rawBindTexture()(GL_TEXTURE_2D, 0);
    return true;
}

void LinuxOpenGLUnityImportManager::releaseLocked(
    bool haveUnityContext, bool abandonWithDevice)
{
    if (!m_attachedProducer || !haveUnityContext) {
        if (abandonWithDevice) {
            // These names belong to Unity's GL context. If that context is no
            // longer current during its shutdown event, context teardown owns
            // their destruction and the CPU-side names must be forgotten.
            for (UnityImport& imported : m_imports)
                imported = {};
            m_published.store(0);
            m_imported.store(false);
        }
        return;
    }
    for (UnityImport& imported : m_imports) {
        if (imported.texture)
            m_attachedProducer->rawDeleteTextures()(1, &imported.texture);
        if (imported.memoryObject)
            m_attachedProducer->deleteMemoryObjects()(1, &imported.memoryObject);
        imported = {};
    }
    m_published.store(0);
    m_imported.store(false);
}

void LinuxOpenGLUnityImportManager::release(
    bool haveUnityContext, bool abandonWithDevice)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    releaseLocked(haveUnityContext, abandonWithDevice);
}

void LinuxOpenGLUnityImportManager::prepareImportsForPluginUnload()
{
    beginShutdown();
    // If Unity's context is current, delete the imported names now. Otherwise
    // plugin unload is the terminal lifecycle event: forget CPU-side names and
    // delegate their storage to Unity's context teardown.
    release(hasRenderThreadContext(), true);
}

void LinuxOpenGLUnityImportManager::refresh()
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !hasRenderThreadContext())
        return;
    if (m_shutdownRequested.load()) {
        releaseLocked(true, false);
        return;
    }
    if (!m_attachedProducer || m_attachedProducer->width() == 0 || m_imported.load())
        return;
    releaseLocked(true, false);
    for (size_t i = 0; i < LinuxDMABufProducer::SlotCount; ++i) {
        if (!importSlotToUnity(i)) {
            releaseLocked(true, false);
            return;
        }
    }
    m_imported.store(true);
    m_published.store(LinuxDMABufProducer::SlotCount);
}

void* LinuxOpenGLUnityImportManager::videoFrame(bool* outUpdated)
{
    if (outUpdated)
        *outUpdated = false;
    if (!m_attachedProducer || !m_imported.load())
        return nullptr;
    size_t index = 0;
    bool updated = false;
    if (!m_attachedProducer->consumeLatest(index, updated))
        return nullptr;
    if (outUpdated)
        *outUpdated = updated;
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>(m_imports[index].texture));
}
