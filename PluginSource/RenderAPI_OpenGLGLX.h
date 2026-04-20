#ifndef RENDER_API_OPENGL_GLX_H
#define RENDER_API_OPENGL_GLX_H

#include "RenderAPI_OpenGLBase.h"
#include "PlatformBase.h"
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <atomic>

#if defined(SUPPORT_DMABUF)
#include <mutex>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#endif

class RenderAPI_OpenGLGLX : public RenderAPI_OpenGLBase
{
public:
    RenderAPI_OpenGLGLX(UnityGfxRenderer apiType);
    virtual ~RenderAPI_OpenGLGLX() { }

    virtual void setVlcContext(libvlc_media_player_t *mp) override;
    virtual void unsetVlcContext(libvlc_media_player_t *mp) override;
    virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
    virtual void retrieveOpenGLContext() override;
    virtual void ensureCurrentContext() override;
    virtual bool makeCurrent(bool current) override;
    virtual void performRenderThreadWork() override;

    static void* get_proc_address(void* /*data*/, const char* procname);

#if defined(SUPPORT_DMABUF)
    void* getVideoFrame(unsigned width, unsigned height, bool* out_updated) override;
#endif

protected:
    Display* m_display = nullptr;
    GLXPbuffer m_pbuffer = None;
    GLXContext m_context = nullptr;
    std::atomic<libvlc_media_player_t*> m_pending_mp{nullptr};
    static GLXContext unity_context;
    static Display* unity_display;

#if defined(SUPPORT_DMABUF)
    // DMA-BUF buffer descriptor (one per triple-buffer slot)
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
    bool m_use_dmabuf = false;
    bool m_unity_textures_imported = false;
    struct gbm_device* m_gbm_device = nullptr;
    int m_drm_fd = -1;
    DMABufBuffer m_dmabuf_buffers[kDMABufSlots];
    unsigned m_dmabuf_width = 0;
    unsigned m_dmabuf_height = 0;

    // Shadowed triple-buffer state (same pattern as Vulkan class)
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

    // DMA-BUF helpers
    bool initDMABuf();
    void releaseDMABufResources();
    bool createDMABufBuffer(DMABufBuffer& buf, unsigned w, unsigned h);
    bool importDMABufToUnityContext(DMABufBuffer& buf, unsigned w, unsigned h);
    bool loadMemoryObjectExtensions();

    // DMA-BUF-specific VLC callbacks
    static bool dmabuf_setup(void** opaque, const libvlc_video_setup_device_cfg_t*,
                             libvlc_video_setup_device_info_t*);
    static void dmabuf_cleanup(void* opaque);
    static bool dmabuf_resize(void* opaque, const libvlc_video_render_cfg_t* cfg,
                              libvlc_video_output_cfg_t* output);
    static void dmabuf_swap(void* opaque);
#endif
};

#endif /* RENDER_API_OPENGL_GLX_H */
