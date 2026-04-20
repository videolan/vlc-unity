#ifndef RENDER_API_OPENGL_LINUX_EGL_H
#define RENDER_API_OPENGL_LINUX_EGL_H

#include "RenderAPI_OpenGLEGL.h"
#include <GL/glx.h>
#include <atomic>
#include <mutex>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>

class RenderAPI_OpenGLLinuxEGL : public RenderAPI_OpenEGL
{
public:
    RenderAPI_OpenGLLinuxEGL(UnityGfxRenderer apiType);
    virtual ~RenderAPI_OpenGLLinuxEGL() { }

    void setVlcContext(libvlc_media_player_t *mp) override;
    void unsetVlcContext(libvlc_media_player_t *mp) override;
    void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
    void retrieveOpenGLContext() override;
    void* getVideoFrame(unsigned width, unsigned height, bool* out_updated) override;
    void performRenderThreadWork() override;

    static void* get_proc_address_desktop(void* data, const char* procname);

private:
    // DRM/GBM state
    int m_drm_fd = -1;
    struct gbm_device* m_gbm_device = nullptr;
    std::atomic<libvlc_media_player_t*> m_pending_mp{nullptr};

    // Unity context flag — static so it's shared across instances
    // (EarlyRenderAPI sets it, per-player instances read it)
    static bool m_unity_context_ready;

    // DMA-BUF buffer descriptor
    struct DMABufBuffer {
        struct gbm_bo* bo = nullptr;
        int dmabuf_fd = -1;
        GLuint vlc_mem_obj = 0;
        GLuint vlc_tex = 0;
        GLuint vlc_fbo = 0;
        GLuint unity_mem_obj = 0;
        GLuint unity_tex = 0;
        uint32_t stride = 0;
        uint64_t size = 0;
    };

    // DMA-BUF state
    static constexpr size_t kDMABufSlots = 3;
    bool m_unity_textures_imported = false;
    DMABufBuffer m_dmabuf_buffers[kDMABufSlots];
    unsigned m_dmabuf_width = 0;
    unsigned m_dmabuf_height = 0;

    // Triple-buffer indices
    std::mutex m_dmabuf_lock;
    size_t m_idx_render = 0;
    size_t m_idx_swap = 1;
    size_t m_idx_display = 2;
    bool m_updated = false;

    // GL_EXT_memory_object_fd function pointers
    PFNGLCREATEMEMORYOBJECTSEXTPROC glCreateMemoryObjectsEXT = nullptr;
    PFNGLTEXSTORAGEMEM2DEXTPROC glTexStorageMem2DEXT = nullptr;
    PFNGLIMPORTMEMORYFDEXTPROC glImportMemoryFdEXT = nullptr;
    PFNGLDELETEMEMORYOBJECTSEXTPROC glDeleteMemoryObjectsEXT = nullptr;
    typedef void (*PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_)(GLuint, GLenum, const GLint*);
    PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_ glMemoryObjectParameterivEXT = nullptr;

    // Raw GL function pointers (bypass Unity's GL wrapper for Unity-context ops)
    typedef void (*PFNGLGENTEXTURESPROC_RAW)(GLsizei, GLuint*);
    typedef void (*PFNGLBINDTEXTUREPROC_RAW)(GLenum, GLuint);
    typedef void (*PFNGLTEXPARAMETERIPROC_RAW)(GLenum, GLenum, GLint);
    typedef void (*PFNGLDELETETEXTURESPROC_RAW)(GLsizei, const GLuint*);
    PFNGLGENTEXTURESPROC_RAW raw_glGenTextures = nullptr;
    PFNGLBINDTEXTUREPROC_RAW raw_glBindTexture = nullptr;
    PFNGLTEXPARAMETERIPROC_RAW raw_glTexParameteri = nullptr;
    PFNGLDELETETEXTURESPROC_RAW raw_glDeleteTextures = nullptr;

    // Helpers
    bool initDRMAndGBM();
    bool loadMemoryObjectExtensions();
    bool createDMABufBuffer(DMABufBuffer& buf, unsigned w, unsigned h);
    bool importDMABufToUnityContext(DMABufBuffer& buf, unsigned w, unsigned h);
    void releaseResources();

    // DMA-BUF VLC callbacks
    static bool dmabuf_setup(void** opaque, const libvlc_video_setup_device_cfg_t*,
                             libvlc_video_setup_device_info_t*);
    static void dmabuf_cleanup(void* opaque);
    static bool dmabuf_resize(void* opaque, const libvlc_video_render_cfg_t* cfg,
                              libvlc_video_output_cfg_t* output);
    static void dmabuf_swap(void* opaque);
};

#endif /* RENDER_API_OPENGL_LINUX_EGL_H */
