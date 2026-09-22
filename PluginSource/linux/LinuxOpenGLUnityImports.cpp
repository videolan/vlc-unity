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

void LinuxOpenGLUnityImportManager::beginShutdown()
{
    m_shutdownRequested = true;
}

bool LinuxOpenGLUnityImportManager::finishShutdownOnRenderThread()
{
    if (!m_shutdownRequested)
        return false;
    if (m_shutdownComplete)
        return true;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_importContext.handle != 0) {
        const LinuxOpenGLContextIdentity current =
            currentRenderThreadContextIdentity();
        if (current.handle == 0)
            return false;
        if (current.handle == m_importContext.handle &&
            current.isEgl == m_importContext.isEgl) {
            destroyImportsLocked(m_imports);
        } else {
            // Unity replaced the GL context (for example while leaving Play
            // Mode). The old context owns those names and will destroy them;
            // issuing deletes against the replacement could hit reused names.
            abandonCurrentImportsLocked();
        }
    }
    m_imported.store(false);
    m_shutdownComplete = true;
    return true;
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

void LinuxOpenGLUnityImportManager::destroyImportsLocked(
    std::array<UnityImport, LinuxDMABufProducer::SlotCount>& imports)
{
    for (UnityImport& imported : imports) {
        if (m_attachedProducer && imported.texture)
            m_attachedProducer->rawDeleteTextures()(1, &imported.texture);
        if (m_attachedProducer && imported.memoryObject)
            m_attachedProducer->deleteMemoryObjects()(1, &imported.memoryObject);
        imported = {};
    }
    m_importContext = {};
}

void LinuxOpenGLUnityImportManager::abandonCurrentImportsLocked()
{
    for (UnityImport& imported : m_imports)
        imported = {};
    m_imported.store(false);
    m_importContext = {};
}

void LinuxOpenGLUnityImportManager::abandonImports()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    abandonCurrentImportsLocked();
}

void LinuxOpenGLUnityImportManager::prepareImportsForPluginUnload()
{
    beginShutdown();
    abandonImports();
    m_shutdownComplete = true;
}

void LinuxOpenGLUnityImportManager::refresh()
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !hasRenderThreadContext())
        return;
    if (m_shutdownRequested)
        return;

    if (!m_attachedProducer || m_attachedProducer->width() == 0 || m_imported.load())
        return;

    abandonCurrentImportsLocked();
    for (size_t i = 0; i < LinuxDMABufProducer::SlotCount; ++i) {
        if (!importSlotToUnity(i)) {
            destroyImportsLocked(m_imports);
            m_imported.store(false);
            return;
        }
    }
    m_importContext = currentRenderThreadContextIdentity();
    m_imported.store(true);
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
