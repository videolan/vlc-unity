#pragma once

#include "LinuxVideoOutput.h"

#include "LinuxGBMDevice.h"
#include "RenderAPI_OpenGLLinuxDMABuf.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

struct gbm_bo;

struct LinuxDMABufSlot
{
    gbm_bo* bo = nullptr;
    int fd = -1;
    uint32_t format = 0;
    uint64_t modifier = 0;
    uint32_t planeCount = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint64_t size = 0;
    unsigned width = 0;
    unsigned height = 0;
    EGLImageKHR eglImage = EGL_NO_IMAGE_KHR;
    GLuint memoryObject = 0;
    GLuint texture = 0;
    GLuint framebuffer = 0;
};

class ILinuxDMABufProducerContext
{
public:
    virtual ~ILinuxDMABufProducerContext() = default;
    virtual bool producerMakeCurrent(bool current) = 0;
    virtual void* producerLoadProc(const char* name) = 0;
    virtual EGLDisplay producerEGLDisplay() const { return EGL_NO_DISPLAY; }
};

class ILinuxDMABufProducerObserver
{
public:
    virtual ~ILinuxDMABufProducerObserver() = default;
    virtual bool onProducerSetup() { return true; }
    virtual void onProducerCleanup() {}
    virtual bool beginDMABufSlotDestruction() { return true; }
    virtual void endDMABufSlotDestruction() {}
    virtual bool onBeforeProducerSwap(GLuint, unsigned, unsigned) { return true; }
    virtual bool onDMABufSlotCreated(size_t, const LinuxDMABufSlot&) { return true; }
    virtual void onDMABufSlotDestroying(size_t, const LinuxDMABufSlot&) {}
};

// Optional extension for Vulkan consumers that drive slot rotation and
// back-pressure themselves; OpenGL producers pass nullptr.
class ILinuxVulkanSlotObserver
{
public:
    virtual ~ILinuxVulkanSlotObserver() = default;
    virtual bool beginVulkanRendering(size_t, const LinuxDMABufSlot&) = 0;
    virtual bool advanceVulkanSlots(size_t rendered,
                                    const LinuxDMABufSlot& renderedSlot,
                                    size_t& next) = 0;
};

class LinuxDMABufProducer
{
public:
    static constexpr size_t SlotCount = 3;

    LinuxDMABufProducer(const char* logPrefix,
                        LinuxGBMDevice& gbm,
                        ILinuxDMABufProducerContext& context,
                        ILinuxDMABufProducerObserver* observer,
                        ILinuxVulkanSlotObserver* vulkanSlots,
                        bool finishBeforeOpenGLPublish);
    ~LinuxDMABufProducer();

    LinuxDMABufProducer(const LinuxDMABufProducer&) = delete;
    LinuxDMABufProducer& operator=(const LinuxDMABufProducer&) = delete;

    bool initialize();
    bool probe(unsigned width = 64, unsigned height = 64);
    bool setVlcContext(libvlc_media_player_t* mediaPlayer, LinuxVideoOutput* output = nullptr);
    void unsetVlcContext(libvlc_media_player_t* mediaPlayer);
    void release();

    bool consumeLatest(size_t& slot, bool& updated);
    const LinuxDMABufSlot& slot(size_t index) const { return m_slots[index]; }
    unsigned width() const { return m_width.load(); }
    unsigned height() const { return m_height.load(); }

    PFNGLCREATEMEMORYOBJECTSEXTPROC createMemoryObjects() const { return m_createMemoryObjects; }
    PFNGLTEXSTORAGEMEM2DEXTPROC textureStorageMemory() const { return m_textureStorageMemory; }
    PFNGLIMPORTMEMORYFDEXTPROC importMemoryFd() const { return m_importMemoryFd; }
    PFNGLDELETEMEMORYOBJECTSEXTPROC deleteMemoryObjects() const { return m_deleteMemoryObjects; }
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_ memoryObjectParameter() const { return m_memoryObjectParameter; }
    PFNGLGENTEXTURESPROC_RAW rawGenTextures() const { return m_rawGenTextures; }
    PFNGLBINDTEXTUREPROC_RAW rawBindTexture() const { return m_rawBindTexture; }
    PFNGLTEXPARAMETERIPROC_RAW rawTextureParameter() const { return m_rawTextureParameter; }
    PFNGLDELETETEXTURESPROC_RAW rawDeleteTextures() const { return m_rawDeleteTextures; }

private:
    static bool setupCallback(void** opaque,
                              const libvlc_video_setup_device_cfg_t*,
                              libvlc_video_setup_device_info_t*);
    static void cleanupCallback(void* opaque);
    static bool resizeCallback(void* opaque,
                               const libvlc_video_render_cfg_t*,
                               libvlc_video_output_cfg_t*);
    static void swapCallback(void* opaque);
    static bool makeCurrentCallback(void* opaque, bool current);
    static void* getProcCallback(void* opaque, const char* name);

    bool resize(const libvlc_video_render_cfg_t*, libvlc_video_output_cfg_t*);
    void swap();
    bool initializeEGLImageImport();
    bool validateEGLImageFormatSupport();
    bool importEGLImage(LinuxDMABufSlot& slot);
    bool createSlot(size_t index, unsigned width, unsigned height);
    bool destroySlots(bool haveCurrentContext);

    const char* m_logPrefix;
    LinuxGBMDevice& m_gbm;
    ILinuxDMABufProducerContext& m_context;
    ILinuxDMABufProducerObserver* m_observer;
    ILinuxVulkanSlotObserver* m_vulkanSlots;
    bool m_finishBeforeOpenGLPublish;
    std::array<LinuxDMABufSlot, SlotCount> m_slots;
    std::mutex m_mutex;
    std::atomic<unsigned> m_width { 0 };
    std::atomic<unsigned> m_height { 0 };
    size_t m_renderIndex = 0;
    size_t m_swapIndex = 1;
    size_t m_displayIndex = 2;
    bool m_updated = false;
    bool m_initialized = false;

    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    PFNEGLCREATEIMAGEKHRPROC m_eglCreateImageKHR = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC m_eglDestroyImageKHR = nullptr;
    PFNEGLQUERYDMABUFFORMATSEXTPROC m_eglQueryDmaBufFormatsEXT = nullptr;
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC m_eglQueryDmaBufModifiersEXT = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC m_glEGLImageTargetTexture2DOES = nullptr;
    PFNGLCREATEMEMORYOBJECTSEXTPROC m_createMemoryObjects = nullptr;
    PFNGLTEXSTORAGEMEM2DEXTPROC m_textureStorageMemory = nullptr;
    PFNGLIMPORTMEMORYFDEXTPROC m_importMemoryFd = nullptr;
    PFNGLDELETEMEMORYOBJECTSEXTPROC m_deleteMemoryObjects = nullptr;
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_ m_memoryObjectParameter = nullptr;
    PFNGLGENTEXTURESPROC_RAW m_rawGenTextures = nullptr;
    PFNGLBINDTEXTUREPROC_RAW m_rawBindTexture = nullptr;
    PFNGLTEXPARAMETERIPROC_RAW m_rawTextureParameter = nullptr;
    PFNGLDELETETEXTURESPROC_RAW m_rawDeleteTextures = nullptr;
};
