#include "LinuxDMABufProducer.h"
#include "Log.h"

#include <algorithm>
#include <cstring>
#include <drm_fourcc.h>
#include <gbm.h>
#include <unistd.h>
#include <vector>

namespace {

void* loadProducerProc(const char* name, void* opaque)
{
    return static_cast<ILinuxDMABufProducerContext*>(opaque)->producerLoadProc(name);
}

bool extensionListContains(const char* extensions, const char* name)
{
    if (!extensions || !name)
        return false;
    const size_t length = strlen(name);
    const char* current = extensions;
    while ((current = strstr(current, name)) != nullptr) {
        const bool startsAtBoundary = current == extensions || current[-1] == ' ';
        const char next = current[length];
        const bool endsAtBoundary = next == '\0' || next == ' ';
        if (startsAtBoundary && endsAtBoundary)
            return true;
        current += length;
    }
    return false;
}

} // namespace

LinuxDMABufProducer::LinuxDMABufProducer(
    const char* logPrefix, LinuxGBMDevice& gbm,
    ILinuxDMABufProducerContext& context,
    ILinuxDMABufProducerObserver* observer,
    ILinuxVulkanSlotObserver* vulkanSlots,
    bool finishBeforeOpenGLPublish)
    : m_logPrefix(logPrefix), m_gbm(gbm), m_context(context),
      m_observer(observer), m_vulkanSlots(vulkanSlots),
      m_finishBeforeOpenGLPublish(finishBeforeOpenGLPublish)
{
}

LinuxDMABufProducer::~LinuxDMABufProducer()
{
    release();
}

bool LinuxDMABufProducer::initialize()
{
    if (!m_gbm || !m_context.producerMakeCurrent(true))
        return false;
    bool functions = false;
    if (m_vulkanSlots) {
        functions = initializeEGLImageImport();
    } else {
        static const char* requiredExtensions[] = {
            "GL_EXT_memory_object", "GL_EXT_memory_object_fd"
        };
        const bool extensions = LinuxGLHasExtensions(
            m_logPrefix, loadProducerProc, &m_context, requiredExtensions,
            sizeof(requiredExtensions) / sizeof(requiredExtensions[0]));
        functions = extensions && LinuxGLLoadMemoryObjectFunctions(
            m_logPrefix, loadProducerProc, &m_context,
            m_createMemoryObjects, m_textureStorageMemory, m_importMemoryFd,
            m_deleteMemoryObjects, m_memoryObjectParameter, m_rawGenTextures,
            m_rawBindTexture, m_rawTextureParameter, m_rawDeleteTextures);
    }
    m_context.producerMakeCurrent(false);
    m_initialized = functions;
    return functions;
}

bool LinuxDMABufProducer::initializeEGLImageImport()
{
    m_eglDisplay = m_context.producerEGLDisplay();
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        DEBUG("[%s] EGL display is unavailable for explicit DMA-BUF import",
              m_logPrefix);
        return false;
    }

    const char* eglExtensions = eglQueryString(m_eglDisplay, EGL_EXTENSIONS);
    static const char* requiredEGLExtensions[] = {
        "EGL_KHR_image_base",
        "EGL_EXT_image_dma_buf_import",
        "EGL_EXT_image_dma_buf_import_modifiers",
    };
    for (const char* extension : requiredEGLExtensions) {
        if (!extensionListContains(eglExtensions, extension)) {
            DEBUG("[%s] missing EGL extension %s", m_logPrefix, extension);
            return false;
        }
    }

    static const char* requiredGLExtensions[] = { "GL_OES_EGL_image" };
    if (!LinuxGLHasExtensions(
            m_logPrefix, loadProducerProc, &m_context, requiredGLExtensions,
            sizeof(requiredGLExtensions) / sizeof(requiredGLExtensions[0]))) {
        return false;
    }

    m_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        m_context.producerLoadProc("eglCreateImageKHR"));
    m_eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        m_context.producerLoadProc("eglDestroyImageKHR"));
    m_eglQueryDmaBufFormatsEXT =
        reinterpret_cast<PFNEGLQUERYDMABUFFORMATSEXTPROC>(
            m_context.producerLoadProc("eglQueryDmaBufFormatsEXT"));
    m_eglQueryDmaBufModifiersEXT =
        reinterpret_cast<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>(
            m_context.producerLoadProc("eglQueryDmaBufModifiersEXT"));
    m_glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            m_context.producerLoadProc("glEGLImageTargetTexture2DOES"));
    if (!m_eglCreateImageKHR || !m_eglDestroyImageKHR ||
        !m_eglQueryDmaBufFormatsEXT || !m_eglQueryDmaBufModifiersEXT ||
        !m_glEGLImageTargetTexture2DOES) {
        DEBUG("[%s] required EGL DMA-BUF image functions are unavailable",
              m_logPrefix);
        return false;
    }

    if (!validateEGLImageFormatSupport())
        return false;

    DEBUG("[%s] explicit EGLImage DMA-BUF import initialized", m_logPrefix);
    return true;
}

bool LinuxDMABufProducer::validateEGLImageFormatSupport()
{
    EGLint formatCount = 0;
    if (!m_eglQueryDmaBufFormatsEXT(
            m_eglDisplay, 0, nullptr, &formatCount) ||
        formatCount <= 0) {
        DEBUG("[%s] EGL DMA-BUF format query failed", m_logPrefix);
        return false;
    }

    std::vector<EGLint> formats(static_cast<size_t>(formatCount));
    EGLint returnedFormatCount = formatCount;
    if (!m_eglQueryDmaBufFormatsEXT(
            m_eglDisplay, formatCount, formats.data(),
            &returnedFormatCount)) {
        DEBUG("[%s] EGL DMA-BUF format enumeration failed", m_logPrefix);
        return false;
    }
    const EGLint format = static_cast<EGLint>(DRM_FORMAT_ABGR8888);
    const size_t validFormatCount = static_cast<size_t>(
        std::max(0, std::min(formatCount, returnedFormatCount)));
    if (std::find(
            formats.begin(), formats.begin() + validFormatCount, format) ==
        formats.begin() + validFormatCount) {
        DEBUG("[%s] EGL does not support DRM_FORMAT_ABGR8888 DMA-BUF import",
              m_logPrefix);
        return false;
    }

    EGLint modifierCount = 0;
    if (!m_eglQueryDmaBufModifiersEXT(
            m_eglDisplay, format, 0, nullptr, nullptr, &modifierCount) ||
        modifierCount <= 0) {
        DEBUG("[%s] EGL DMA-BUF modifier query failed for DRM_FORMAT_ABGR8888",
              m_logPrefix);
        return false;
    }

    std::vector<EGLuint64KHR> modifiers(static_cast<size_t>(modifierCount));
    std::vector<EGLBoolean> externalOnly(static_cast<size_t>(modifierCount));
    EGLint returnedModifierCount = modifierCount;
    if (!m_eglQueryDmaBufModifiersEXT(
            m_eglDisplay, format, modifierCount, modifiers.data(),
            externalOnly.data(), &returnedModifierCount)) {
        DEBUG("[%s] EGL DMA-BUF modifier enumeration failed", m_logPrefix);
        return false;
    }

    const size_t validModifierCount = static_cast<size_t>(
        std::max(0, std::min(modifierCount, returnedModifierCount)));
    for (size_t i = 0; i < validModifierCount; ++i) {
        if (modifiers[i] == DRM_FORMAT_MOD_LINEAR &&
            externalOnly[i] == EGL_FALSE) {
            return true;
        }
    }

    DEBUG("[%s] EGL cannot import linear DRM_FORMAT_ABGR8888 as GL_TEXTURE_2D",
          m_logPrefix);
    return false;
}

bool LinuxDMABufProducer::importEGLImage(LinuxDMABufSlot& buffer)
{
    const EGLint attributes[] = {
        EGL_WIDTH, static_cast<EGLint>(buffer.width),
        EGL_HEIGHT, static_cast<EGLint>(buffer.height),
        EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(buffer.format),
        EGL_DMA_BUF_PLANE0_FD_EXT, static_cast<EGLint>(buffer.fd),
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLint>(buffer.offset),
        EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(buffer.stride),
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
        static_cast<EGLint>(static_cast<uint32_t>(buffer.modifier)),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
        static_cast<EGLint>(static_cast<uint32_t>(buffer.modifier >> 32)),
        EGL_NONE
    };
    buffer.eglImage = m_eglCreateImageKHR(
        m_eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr,
        attributes);
    if (buffer.eglImage == EGL_NO_IMAGE_KHR) {
        DEBUG("[%s] EGLImage DMA-BUF import failed: EGL error=0x%x",
              m_logPrefix, eglGetError());
        return false;
    }

    clearGlErrors();
    m_glEGLImageTargetTexture2DOES(
        GL_TEXTURE_2D, reinterpret_cast<GLeglImageOES>(buffer.eglImage));
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        DEBUG("[%s] EGLImage texture binding failed: GL error=0x%x",
              m_logPrefix, error);
        m_eglDestroyImageKHR(m_eglDisplay, buffer.eglImage);
        buffer.eglImage = EGL_NO_IMAGE_KHR;
        return false;
    }

    DEBUG("[%s] EGLImage imported for VLC: tex=%u format=0x%x modifier=0x%lx offset=%u stride=%u",
          m_logPrefix, buffer.texture, buffer.format,
          static_cast<unsigned long>(buffer.modifier), buffer.offset,
          buffer.stride);
    return true;
}

bool LinuxDMABufProducer::probe(unsigned probeWidth, unsigned probeHeight)
{
    if (!m_initialized || !m_context.producerMakeCurrent(true))
        return false;
    bool result = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!destroySlots(true)) {
            m_context.producerMakeCurrent(false);
            return false;
        }
        result = createSlot(0, probeWidth, probeHeight);
        (void)destroySlots(true);
    }
    m_context.producerMakeCurrent(false);
    return result;
}

bool LinuxDMABufProducer::setVlcContext(libvlc_media_player_t* mediaPlayer, LinuxVideoOutput* output)
{
    if (!m_initialized || !mediaPlayer)
        return false;
    if (output) {
        output->configure({setupCallback, cleanupCallback, resizeCallback,
            swapCallback, makeCurrentCallback, getProcCallback, this});
        return true;
    }
    return libvlc_video_set_output_callbacks(
        mediaPlayer, libvlc_video_engine_opengl, setupCallback, cleanupCallback,
        nullptr, resizeCallback, swapCallback, makeCurrentCallback,
        getProcCallback, nullptr, nullptr, this);
}

void LinuxDMABufProducer::unsetVlcContext(libvlc_media_player_t* mediaPlayer)
{
    if (!mediaPlayer)
        return;
    libvlc_video_set_output_callbacks(
        mediaPlayer, libvlc_video_engine_disable, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}

bool LinuxDMABufProducer::setupCallback(
    void** opaque, const libvlc_video_setup_device_cfg_t*,
    libvlc_video_setup_device_info_t*)
{
    if (!opaque || !*opaque)
        return false;
    auto* producer = static_cast<LinuxDMABufProducer*>(*opaque);
    producer->m_width.store(0);
    producer->m_height.store(0);
    return !producer->m_observer || producer->m_observer->onProducerSetup();
}

void LinuxDMABufProducer::cleanupCallback(void* opaque)
{
    auto* producer = static_cast<LinuxDMABufProducer*>(opaque);
    if (!producer)
        return;
    if (producer->m_observer)
        producer->m_observer->onProducerCleanup();
    const bool current = producer->m_context.producerMakeCurrent(true);
    if (!current) {
        DEBUG("[%s] deferring slot cleanup because the producer context is not current",
              producer->m_logPrefix);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(producer->m_mutex);
        (void)producer->destroySlots(true);
    }
    producer->m_context.producerMakeCurrent(false);
}

bool LinuxDMABufProducer::resizeCallback(
    void* opaque, const libvlc_video_render_cfg_t* config,
    libvlc_video_output_cfg_t* output)
{
    auto* producer = static_cast<LinuxDMABufProducer*>(opaque);
    return producer && producer->resize(config, output);
}

void LinuxDMABufProducer::swapCallback(void* opaque)
{
    auto* producer = static_cast<LinuxDMABufProducer*>(opaque);
    if (producer)
        producer->swap();
}

bool LinuxDMABufProducer::makeCurrentCallback(void* opaque, bool current)
{
    auto* producer = static_cast<LinuxDMABufProducer*>(opaque);
    return producer && producer->m_context.producerMakeCurrent(current);
}

void* LinuxDMABufProducer::getProcCallback(void* opaque, const char* name)
{
    auto* producer = static_cast<LinuxDMABufProducer*>(opaque);
    return producer ? producer->m_context.producerLoadProc(name) : nullptr;
}

bool LinuxDMABufProducer::resize(
    const libvlc_video_render_cfg_t* config,
    libvlc_video_output_cfg_t* output)
{
    if (!config || !output || !m_context.producerMakeCurrent(true))
        return false;
    bool ok = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (config->width != m_width.load() ||
            config->height != m_height.load()) {
            const bool oldSlotsDestroyed = destroySlots(true);
            ok = oldSlotsDestroyed;
            for (size_t i = 0; ok && i < SlotCount; ++i) {
                if (!createSlot(i, config->width, config->height)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                if (oldSlotsDestroyed)
                    (void)destroySlots(true);
            } else {
                m_width.store(config->width);
                m_height.store(config->height);
            }
        }
        if (ok) {
            m_renderIndex = 0;
            m_swapIndex = 1;
            m_displayIndex = 2;
            m_updated = false;
            if (m_vulkanSlots &&
                !m_vulkanSlots->beginVulkanRendering(0, m_slots[0])) {
                ok = false;
            }
            if (ok)
                glBindFramebuffer(GL_FRAMEBUFFER, m_slots[0].framebuffer);
        }
    }

    if (ok) {
        output->u.opengl_format = GL_RGBA;
        output->full_range = true;
        output->colorspace = libvlc_video_colorspace_BT709;
        output->primaries = libvlc_video_primaries_BT709;
        output->transfer = libvlc_video_transfer_func_SRGB;
        output->orientation = libvlc_video_orient_bottom_right;
    }
    m_context.producerMakeCurrent(false);
    return ok;
}

bool LinuxDMABufProducer::createSlot(
    size_t index, unsigned bufferWidth, unsigned bufferHeight)
{
    LinuxDMABufSlot& buffer = m_slots[index];
    if (m_vulkanSlots) {
        const uint64_t modifiers[] = { DRM_FORMAT_MOD_LINEAR };
        buffer.bo = gbm_bo_create_with_modifiers(
            m_gbm.get(), bufferWidth, bufferHeight, GBM_FORMAT_ABGR8888,
            modifiers, 1);
    } else {
        buffer.bo = gbm_bo_create(
            m_gbm.get(), bufferWidth, bufferHeight, GBM_FORMAT_ABGR8888,
            GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    }
    if (!buffer.bo) {
        DEBUG("[%s] GBM buffer allocation failed for %ux%u",
              m_logPrefix, bufferWidth, bufferHeight);
        return false;
    }
    buffer.format = gbm_bo_get_format(buffer.bo);
    buffer.width = bufferWidth;
    buffer.height = bufferHeight;
    buffer.modifier = gbm_bo_get_modifier(buffer.bo);
    buffer.planeCount = gbm_bo_get_plane_count(buffer.bo);
    const bool unsupportedModifier = m_vulkanSlots
        ? buffer.modifier != DRM_FORMAT_MOD_LINEAR
        : buffer.modifier != DRM_FORMAT_MOD_LINEAR &&
              buffer.modifier != DRM_FORMAT_MOD_INVALID;
    if (unsupportedModifier || buffer.planeCount != 1) {
        DEBUG("[%s] unsupported GBM layout: format=0x%x modifier=0x%lx planes=%u",
              m_logPrefix, buffer.format,
              static_cast<unsigned long>(buffer.modifier), buffer.planeCount);
        return false;
    }
    buffer.fd = gbm_bo_get_fd_for_plane(buffer.bo, 0);
    buffer.stride = gbm_bo_get_stride_for_plane(buffer.bo, 0);
    buffer.offset = gbm_bo_get_offset(buffer.bo, 0);
    if (buffer.fd < 0)
        return false;
    const off_t allocationSize = lseek(buffer.fd, 0, SEEK_END);
    (void)lseek(buffer.fd, 0, SEEK_SET);
    buffer.size = allocationSize > 0
        ? static_cast<uint64_t>(allocationSize)
        : static_cast<uint64_t>(buffer.offset) +
          static_cast<uint64_t>(buffer.stride) * bufferHeight;

    glGenTextures(1, &buffer.texture);
    glBindTexture(GL_TEXTURE_2D, buffer.texture);
    const bool imported = m_vulkanSlots
        ? importEGLImage(buffer)
        : LinuxGLImportMemoryFd(
              m_logPrefix, m_createMemoryObjects, m_importMemoryFd,
              m_deleteMemoryObjects, m_memoryObjectParameter,
              m_textureStorageMemory, buffer.memoryObject, buffer.texture,
              buffer.fd, buffer.size, bufferWidth, bufferHeight, "VLC");
    if (!imported) {
        return false;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &buffer.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, buffer.texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    DEBUG("[%s] slot %zu: format=0x%x modifier=0x%lx planes=%u fd=%d offset=%u stride=%u size=%lu",
          m_logPrefix, index, buffer.format,
          static_cast<unsigned long>(buffer.modifier), buffer.planeCount,
          buffer.fd, buffer.offset, buffer.stride,
          static_cast<unsigned long>(buffer.size));
    return !m_observer || m_observer->onDMABufSlotCreated(index, buffer);
}

void LinuxDMABufProducer::swap()
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    const unsigned frameWidth = m_width.load();
    const unsigned frameHeight = m_height.load();
    if (!lock.owns_lock() || frameWidth == 0 || frameHeight == 0)
        return;
    LinuxDMABufSlot& rendered = m_slots[m_renderIndex];
    if (m_observer && !m_observer->onBeforeProducerSwap(
            rendered.framebuffer, frameWidth, frameHeight))
        return;

    if (m_vulkanSlots) {
        size_t next = m_renderIndex;
        if (!m_vulkanSlots->advanceVulkanSlots(
                m_renderIndex, rendered, next)) {
            return; // Saturated: re-render in place and do not signal.
        }
        m_renderIndex = next;
        glBindFramebuffer(GL_FRAMEBUFFER, m_slots[next].framebuffer);
        return;
    }

    glFlush();
    if (m_finishBeforeOpenGLPublish) {
        // Preserve the upstream GLX DMA-BUF completion policy. Vulkan never
        // enters this branch; it uses explicit GPU semaphore synchronization.
        glFinish();
    }
    m_updated = true;
    std::swap(m_swapIndex, m_renderIndex);
    glBindFramebuffer(GL_FRAMEBUFFER, m_slots[m_renderIndex].framebuffer);
}

bool LinuxDMABufProducer::consumeLatest(size_t& slotIndex, bool& frameUpdated)
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock() || m_width.load() == 0 || m_height.load() == 0)
        return false;
    frameUpdated = m_updated;
    if (m_updated) {
        std::swap(m_swapIndex, m_displayIndex);
        m_updated = false;
    }
    slotIndex = m_displayIndex;
    return true;
}

bool LinuxDMABufProducer::destroySlots(bool haveCurrentContext)
{
    if (m_observer && !m_observer->beginDMABufSlotDestruction())
        return false;
    for (size_t i = 0; i < SlotCount; ++i) {
        LinuxDMABufSlot& buffer = m_slots[i];
        if (m_observer)
            m_observer->onDMABufSlotDestroying(i, buffer);
        if (haveCurrentContext) {
            if (buffer.framebuffer)
                glDeleteFramebuffers(1, &buffer.framebuffer);
            if (buffer.texture)
                glDeleteTextures(1, &buffer.texture);
            if (buffer.eglImage != EGL_NO_IMAGE_KHR &&
                m_eglDestroyImageKHR && m_eglDisplay != EGL_NO_DISPLAY) {
                m_eglDestroyImageKHR(m_eglDisplay, buffer.eglImage);
                buffer.eglImage = EGL_NO_IMAGE_KHR;
            }
            if (buffer.memoryObject && m_deleteMemoryObjects)
                m_deleteMemoryObjects(1, &buffer.memoryObject);
        }
        if (buffer.fd >= 0)
            close(buffer.fd);
        if (buffer.bo)
            gbm_bo_destroy(buffer.bo);
        buffer = {};
        buffer.fd = -1;
    }
    if (m_observer)
        m_observer->endDMABufSlotDestruction();
    m_width.store(0);
    m_height.store(0);
    m_updated = false;
    return true;
}

void LinuxDMABufProducer::release()
{
    const bool current = m_context.producerMakeCurrent(true);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        (void)destroySlots(current);
    }
    if (current)
        m_context.producerMakeCurrent(false);
    m_initialized = false;
}
