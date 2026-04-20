#include "RenderAPI_OpenGLGLX.h"
#include "Log.h"
#include <cassert>
#include <cstring>

#if defined(SUPPORT_DMABUF)
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB  0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#endif

GLXContext RenderAPI_OpenGLGLX::unity_context = nullptr;
Display* RenderAPI_OpenGLGLX::unity_display = nullptr;

namespace {

static int glx_error_code = 0;
static int x_error_handler(Display* /*dpy*/, XErrorEvent* ev)
{
    glx_error_code = ev->error_code;
    return 0;
}

bool staticMakeCurrent(void* data, bool current)
{
    auto that = static_cast<RenderAPI_OpenGLGLX*>(data);
    return that->makeCurrent(current);
}

}

RenderAPI* CreateRenderAPI_OpenGLGLX(UnityGfxRenderer apiType)
{
    return new RenderAPI_OpenGLGLX(apiType);
}

RenderAPI_OpenGLGLX::RenderAPI_OpenGLGLX(UnityGfxRenderer apiType) :
    RenderAPI_OpenGLBase(apiType)
{
}

bool RenderAPI_OpenGLGLX::makeCurrent(bool current)
{
    if (current)
    {
        GLXContext currentCtx = glXGetCurrentContext();
        if (currentCtx == m_context)
            return true;
        return glXMakeContextCurrent(m_display, m_pbuffer, m_pbuffer, m_context);
    }
    else
    {
        if (glXGetCurrentContext() != m_context)
            return true;
        return glXMakeContextCurrent(m_display, None, None, nullptr);
    }
}

void* RenderAPI_OpenGLGLX::get_proc_address(void* /*data*/, const char* procname)
{
    return reinterpret_cast<void*>(glXGetProcAddressARB(
        reinterpret_cast<const GLubyte*>(procname)));
}

void RenderAPI_OpenGLGLX::setVlcContext(libvlc_media_player_t *mp)
{
    if (unity_context == nullptr) {
        m_pending_mp.store(mp);
        DEBUG("[GLX] Unity context not ready, deferring setVlcContext");
        return;
    }
    m_pending_mp.store(nullptr);

#if defined(SUPPORT_DMABUF)
    if (m_use_dmabuf) {
        DEBUG("[GLX] subscribing to DMA-BUF opengl output callbacks %p", this);
        libvlc_video_set_output_callbacks(mp, libvlc_video_engine_opengl,
            dmabuf_setup, dmabuf_cleanup, nullptr, dmabuf_resize, dmabuf_swap,
            staticMakeCurrent, get_proc_address, nullptr, nullptr, this);
        return;
    }
#endif

    DEBUG("[GLX] subscribing to opengl output callbacks %p", this);
    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_opengl,
        setup, cleanup, nullptr, resize, swap,
        staticMakeCurrent, get_proc_address, nullptr, nullptr, this);
}

void RenderAPI_OpenGLGLX::unsetVlcContext(libvlc_media_player_t *mp)
{
    libvlc_media_player_t* expected = mp;
    if (m_pending_mp.compare_exchange_strong(expected, nullptr))
        return; // Cancelled pending setVlcContext before it was promoted
    // Callbacks were already registered — disable them
    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_disable,
        nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr);
}

void RenderAPI_OpenGLGLX::retrieveOpenGLContext()
{
    if (unity_context == nullptr) {
        unity_context = glXGetCurrentContext();
        if (unity_context == nullptr) {
            DEBUG("[GLX] glXGetCurrentContext failed");
        } else {
            unity_display = glXGetCurrentDisplay();
            DEBUG("[GLX] retrieved Unity OpenGL context %p display %p", unity_context, unity_display);
        }
    }
}

void RenderAPI_OpenGLGLX::ensureCurrentContext()
{
    if (glXGetCurrentContext() == nullptr) {
        makeCurrent(true);
    }
}

void RenderAPI_OpenGLGLX::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    (void)interfaces;
    if (type == kUnityGfxDeviceEventInitialize) {
        if (m_context) {
            return;
        }
        DEBUG("[GLX] Entering ProcessDeviceEvent with kUnityGfxDeviceEventInitialize");

        if (unity_context == nullptr) {
            DEBUG("[GLX] Failed to retrieve OpenGL Context... aborting.");
            return;
        }

        m_display = unity_display;
        if (m_display == nullptr) {
            DEBUG("[GLX] unity_display is NULL");
            return;
        }

        int screen = DefaultScreen(m_display);

        // Match Unity's direct/indirect rendering mode
        Bool unity_direct = glXIsDirect(m_display, unity_context);
        DEBUG("[GLX] Unity context is %s rendering", unity_direct ? "direct" : "indirect");

        // Query Unity context's FBConfig and render type
        int unity_fbc_id = 0;
        glXQueryContext(m_display, unity_context, GLX_FBCONFIG_ID, &unity_fbc_id);
        DEBUG("[GLX] Unity context FBConfig ID = 0x%x", unity_fbc_id);

        int unity_render_type = 0;
        glXQueryContext(m_display, unity_context, GLX_RENDER_TYPE, &unity_render_type);
        DEBUG("[GLX] Unity context render type = 0x%x", unity_render_type);

        // Gather all RGBA pbuffer-capable FBConfigs
        static const int pb_attribs[] = {
            GLX_RENDER_TYPE, GLX_RGBA_BIT,
            GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_ALPHA_SIZE, 8,
            None
        };
        int fbcount = 0;
        GLXFBConfig* all_fbc = glXChooseFBConfig(m_display, screen, pb_attribs, &fbcount);
        if (!all_fbc || fbcount == 0) {
            DEBUG("[GLX] no PBuffer-capable FBConfigs found");
            return;
        }
        DEBUG("[GLX] found %d pbuffer-capable FBConfigs", fbcount);

        // Sort: put Unity's exact FBConfig first if present
        for (int i = 1; i < fbcount; i++) {
            int id = 0;
            glXGetFBConfigAttrib(m_display, all_fbc[i], GLX_FBCONFIG_ID, &id);
            if (id == unity_fbc_id) {
                GLXFBConfig tmp = all_fbc[0];
                all_fbc[0] = all_fbc[i];
                all_fbc[i] = tmp;
                DEBUG("[GLX] prioritized Unity's FBConfig (id=0x%x)", unity_fbc_id);
                break;
            }
        }

        // Install X error handler
        XSync(m_display, False);
        auto old_handler = XSetErrorHandler(x_error_handler);

        typedef GLXContext (*PFNGLXCREATECONTEXTATTRIBSARBPROC)(
            Display*, GLXFBConfig, GLXContext, Bool, const int*);
        auto glXCreateContextAttribsARB =
            reinterpret_cast<PFNGLXCREATECONTEXTATTRIBSARBPROC>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));

        GLXFBConfig chosen_fbc = nullptr;
        bool shared_context = false;

        // Try each FBConfig until shared context creation succeeds
        for (int fi = 0; fi < fbcount && !m_context; fi++) {
            GLXFBConfig try_fbc = all_fbc[fi];

            if (fi < 5 || fi == fbcount - 1) {
                int fbc_id = 0;
                glXGetFBConfigAttrib(m_display, try_fbc, GLX_FBCONFIG_ID, &fbc_id);
                DEBUG("[GLX] trying FBConfig %d/%d (id=0x%x)", fi + 1, fbcount, fbc_id);
            }

            if (glXCreateContextAttribsARB) {
                static const int profiles[][3] = {
                    {4, 5, GLX_CONTEXT_CORE_PROFILE_BIT_ARB},
                    {3, 3, GLX_CONTEXT_CORE_PROFILE_BIT_ARB},
                    {4, 5, GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB},
                    {3, 3, GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB},
                };
                for (auto& p : profiles) {
                    glx_error_code = 0;
                    const int ctx_attribs[] = {
                        GLX_CONTEXT_MAJOR_VERSION_ARB, p[0],
                        GLX_CONTEXT_MINOR_VERSION_ARB, p[1],
                        GLX_CONTEXT_PROFILE_MASK_ARB,  p[2],
                        None
                    };
                    m_context = glXCreateContextAttribsARB(
                        m_display, try_fbc, unity_context, unity_direct, ctx_attribs);
                    XSync(m_display, False);
                    if (m_context && glx_error_code == 0) {
                        const char* prof = (p[2] == GLX_CONTEXT_CORE_PROFILE_BIT_ARB) ? "core" : "compat";
                        DEBUG("[GLX] Created GL %d.%d %s shared context (fbc %d/%d)",
                              p[0], p[1], prof, fi + 1, fbcount);
                        chosen_fbc = try_fbc;
                        shared_context = true;
                        break;
                    }
                    if (m_context) { glXDestroyContext(m_display, m_context); m_context = nullptr; }
                }
            }

            if (!m_context) {
                glx_error_code = 0;
                m_context = glXCreateNewContext(m_display, try_fbc, GLX_RGBA_TYPE,
                                                 unity_context, unity_direct);
                XSync(m_display, False);
                if (m_context && glx_error_code == 0) {
                    DEBUG("[GLX] Created shared context via glXCreateNewContext (fbc %d/%d)",
                          fi + 1, fbcount);
                    chosen_fbc = try_fbc;
                    shared_context = true;
                } else {
                    if (m_context) { glXDestroyContext(m_display, m_context); m_context = nullptr; }
                }
            }
        }

        // Non-shared context fallback
        if (!m_context) {
            DEBUG("[GLX] all %d FBConfigs failed for shared context, creating non-shared", fbcount);
            chosen_fbc = all_fbc[0];
            glx_error_code = 0;
            m_context = glXCreateNewContext(m_display, chosen_fbc, GLX_RGBA_TYPE,
                                             nullptr, unity_direct);
            XSync(m_display, False);
            if (m_context && glx_error_code == 0) {
                DEBUG("[GLX] created non-shared context");
            } else {
                if (m_context) { glXDestroyContext(m_display, m_context); m_context = nullptr; }
                DEBUG("[GLX] even non-shared context creation failed, X error=%d", glx_error_code);
            }
        }

        XSetErrorHandler(old_handler);

        if (!m_context) {
            XFree(all_fbc);
            DEBUG("[GLX] all context creation attempts failed");
            return;
        }

        // Create pbuffer
        static const int pbuffer_attribs[] = {
            GLX_PBUFFER_WIDTH, 2,
            GLX_PBUFFER_HEIGHT, 2,
            None
        };
        m_pbuffer = glXCreatePbuffer(m_display, chosen_fbc, pbuffer_attribs);
        XFree(all_fbc);
        if (m_pbuffer == None) {
            DEBUG("[GLX] glXCreatePbuffer() failed");
            glXDestroyContext(m_display, m_context);
            m_context = nullptr;
            return;
        }

#if defined(SUPPORT_DMABUF)
        if (!shared_context) {
            DEBUG("[GLX] no shared context, attempting DMA-BUF path");
            if (initDMABuf()) {
                m_use_dmabuf = true;
                DEBUG("[GLX] DMA-BUF zero-copy path initialized successfully");
            } else {
                DEBUG("[GLX] DMA-BUF not available, falling back to non-shared context (textures won't work)");
            }
        }
#else
        if (!shared_context) {
            DEBUG("[GLX] WARNING: non-shared context and no DMA-BUF support compiled in");
        }
#endif

        DEBUG("[GLX] kUnityGfxDeviceEventInitialize success disp=%p pbuf=%lx ctx=%p shared=%d dmabuf=%d",
              m_display, m_pbuffer, m_context, shared_context,
#if defined(SUPPORT_DMABUF)
              m_use_dmabuf
#else
              0
#endif
              );
        libvlc_media_player_t* pending = m_pending_mp.exchange(nullptr);
        if (pending)
            setVlcContext(pending);

    } else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[GLX] kUnityGfxDeviceEventShutdown");
#if defined(SUPPORT_DMABUF)
        releaseDMABufResources();
#endif
        if (m_context) {
            glXDestroyContext(m_display, m_context);
            m_context = nullptr;
        }
        if (m_pbuffer) {
            glXDestroyPbuffer(m_display, m_pbuffer);
            m_pbuffer = None;
        }
    }
}

void RenderAPI_OpenGLGLX::performRenderThreadWork()
{
#if defined(SUPPORT_DMABUF)
    if (!m_use_dmabuf)
        return;

    std::lock_guard<std::mutex> lock(m_dmabuf_lock);

    if (m_unity_textures_imported || m_dmabuf_width == 0 || m_dmabuf_height == 0)
        return;

    if (glXGetCurrentContext() == nullptr) {
        DEBUG("[GLX] no GL context current on render thread");
        return;
    }

    DEBUG("[GLX] importing DMA-BUF textures into Unity context (render thread)");
    for (size_t i = 0; i < kDMABufSlots; i++) {
        if (!importDMABufToUnityContext(m_dmabuf_buffers[i], m_dmabuf_width, m_dmabuf_height)) {
            DEBUG("[GLX] failed to import DMA-BUF buffer %zu into Unity context", i);
            return;
        }
    }
    m_unity_textures_imported = true;
    DEBUG("[GLX] all DMA-BUF textures imported into Unity context");
#endif
}

// ==========================================================================
// DMA-BUF implementation (via GL_EXT_memory_object_fd)
// ==========================================================================

#if defined(SUPPORT_DMABUF)

#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#endif
#ifndef GL_DEDICATED_MEMORY_OBJECT_EXT
#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#endif

bool RenderAPI_OpenGLGLX::loadMemoryObjectExtensions()
{
    glCreateMemoryObjectsEXT = reinterpret_cast<PFNGLCREATEMEMORYOBJECTSEXTPROC>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glCreateMemoryObjectsEXT")));
    glTexStorageMem2DEXT = reinterpret_cast<PFNGLTEXSTORAGEMEM2DEXTPROC>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glTexStorageMem2DEXT")));
    glImportMemoryFdEXT = reinterpret_cast<PFNGLIMPORTMEMORYFDEXTPROC>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glImportMemoryFdEXT")));
    glDeleteMemoryObjectsEXT = reinterpret_cast<PFNGLDELETEMEMORYOBJECTSEXTPROC>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDeleteMemoryObjectsEXT")));
    glMemoryObjectParameterivEXT = reinterpret_cast<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glMemoryObjectParameterivEXT")));

    if (!glCreateMemoryObjectsEXT || !glTexStorageMem2DEXT ||
        !glImportMemoryFdEXT || !glDeleteMemoryObjectsEXT) {
        DEBUG("[GLX] GL_EXT_memory_object_fd extension functions not available");
        return false;
    }

    // Load raw GL core functions (bypass Unity's GL wrapper)
    raw_glGenTextures = reinterpret_cast<PFNGLGENTEXTURESPROC_RAW>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glGenTextures")));
    raw_glBindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC_RAW>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindTexture")));
    raw_glTexParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC_RAW>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glTexParameteri")));
    raw_glDeleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC_RAW>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDeleteTextures")));

    if (!raw_glGenTextures || !raw_glBindTexture || !raw_glTexParameteri) {
        DEBUG("[GLX] failed to load raw GL function pointers");
        return false;
    }

    DEBUG("[GLX] GL_EXT_memory_object_fd extensions loaded");
    return true;
}

bool RenderAPI_OpenGLGLX::initDMABuf()
{
    // Check GL extensions — need a current context
    GLXContext prev_ctx = glXGetCurrentContext();
    GLXDrawable prev_draw = glXGetCurrentDrawable();
    GLXDrawable prev_read = glXGetCurrentReadDrawable();

    makeCurrent(true);

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!extensions ||
        (strstr(extensions, "GL_EXT_memory_object") == nullptr ||
         strstr(extensions, "GL_EXT_memory_object_fd") == nullptr)) {
        DEBUG("[GLX] GL_EXT_memory_object_fd not in extension string");
        glXMakeContextCurrent(m_display, prev_draw, prev_read, prev_ctx);
        return false;
    }

    if (!loadMemoryObjectExtensions()) {
        glXMakeContextCurrent(m_display, prev_draw, prev_read, prev_ctx);
        return false;
    }

    // Restore Unity's context
    glXMakeContextCurrent(m_display, prev_draw, prev_read, prev_ctx);

    // Open DRM render node
    DIR* dir = opendir("/dev/dri");
    if (!dir) {
        DEBUG("[GLX] cannot open /dev/dri");
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "renderD", 7) == 0) {
            char path[280];
            snprintf(path, sizeof(path), "/dev/dri/%.255s", entry->d_name);
            m_drm_fd = open(path, O_RDWR | O_CLOEXEC);
            if (m_drm_fd >= 0) {
                DEBUG("[GLX] opened DRM render node %s (fd=%d)", path, m_drm_fd);
                break;
            }
        }
    }
    closedir(dir);

    if (m_drm_fd < 0) {
        DEBUG("[GLX] no DRM render node found");
        return false;
    }

    m_gbm_device = gbm_create_device(m_drm_fd);
    if (!m_gbm_device) {
        DEBUG("[GLX] gbm_create_device failed");
        close(m_drm_fd);
        m_drm_fd = -1;
        return false;
    }
    DEBUG("[GLX] GBM device created");

    return true;
}

typedef void (*PFN_glMemoryObjectParameterivEXT)(GLuint, GLenum, const GLint*);

static bool importMemoryFd(
    PFNGLCREATEMEMORYOBJECTSEXTPROC glCreateMemoryObjectsEXT,
    PFNGLIMPORTMEMORYFDEXTPROC glImportMemoryFdEXT,
    PFNGLDELETEMEMORYOBJECTSEXTPROC glDeleteMemoryObjectsEXT,
    PFN_glMemoryObjectParameterivEXT glMemoryObjectParameterivEXT,
    PFNGLTEXSTORAGEMEM2DEXTPROC glTexStorageMem2DEXT,
    GLuint& mem_obj, GLuint tex, int dmabuf_fd, uint64_t size,
    unsigned w, unsigned h, const char* label)
{
    glCreateMemoryObjectsEXT(1, &mem_obj);

    // Mark as dedicated allocation (required by some drivers for DMA-BUF)
    if (glMemoryObjectParameterivEXT) {
        GLint dedicated = GL_TRUE;
        glMemoryObjectParameterivEXT(mem_obj, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
    }

    // dup() because glImportMemoryFdEXT takes ownership of the fd
    int import_fd = dup(dmabuf_fd);
    if (import_fd < 0) {
        DEBUG("[GLX] dup(dmabuf_fd) failed for %s import", label);
        if (glDeleteMemoryObjectsEXT && mem_obj) {
            glDeleteMemoryObjectsEXT(1, &mem_obj);
            mem_obj = 0;
        }
        return false;
    }

    glImportMemoryFdEXT(mem_obj, size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, import_fd);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG("[GLX] glImportMemoryFdEXT failed for %s, GL error=0x%x", label, err);
        if (glDeleteMemoryObjectsEXT && mem_obj) {
            glDeleteMemoryObjectsEXT(1, &mem_obj);
            mem_obj = 0;
        }
        close(import_fd);
        return false;
    }

    glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, w, h, mem_obj, 0);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG("[GLX] glTexStorageMem2DEXT failed for %s, GL error=0x%x (size=%lu, %ux%u)",
              label, err, (unsigned long)size, w, h);
        if (glDeleteMemoryObjectsEXT && mem_obj) {
            glDeleteMemoryObjectsEXT(1, &mem_obj);
            mem_obj = 0;
        }
        return false;
    }

    DEBUG("[GLX] memory object imported for %s: tex=%u mem=%u size=%lu",
          label, tex, mem_obj, (unsigned long)size);
    return true;
}

bool RenderAPI_OpenGLGLX::createDMABufBuffer(DMABufBuffer& buf, unsigned w, unsigned h)
{
    buf.bo = gbm_bo_create(m_gbm_device, w, h, GBM_FORMAT_ABGR8888,
                           GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!buf.bo) {
        DEBUG("[GLX] gbm_bo_create failed %ux%u", w, h);
        return false;
    }

    buf.dmabuf_fd = gbm_bo_get_fd(buf.bo);
    if (buf.dmabuf_fd < 0) {
        DEBUG("[GLX] gbm_bo_get_fd failed");
        gbm_bo_destroy(buf.bo);
        buf.bo = nullptr;
        return false;
    }

    buf.stride = gbm_bo_get_stride(buf.bo);

    // Get true DMA-BUF allocation size via lseek (stride*height may be too small)
    off_t real_size = lseek(buf.dmabuf_fd, 0, SEEK_END);
    lseek(buf.dmabuf_fd, 0, SEEK_SET);
    if (real_size <= 0) {
        // Fallback to stride * height
        buf.size = (uint64_t)buf.stride * h;
        DEBUG("[GLX] lseek failed, using stride*height=%lu", (unsigned long)buf.size);
    } else {
        buf.size = (uint64_t)real_size;
    }

    DEBUG("[GLX] DMA-BUF: fd=%d stride=%u size=%lu %ux%u",
          buf.dmabuf_fd, buf.stride, (unsigned long)buf.size, w, h);

    // Import into VLC's GL context
    glGenTextures(1, &buf.vlc_tex);
    glBindTexture(GL_TEXTURE_2D, buf.vlc_tex);

    if (!importMemoryFd(glCreateMemoryObjectsEXT, glImportMemoryFdEXT, glDeleteMemoryObjectsEXT,
                        glMemoryObjectParameterivEXT, glTexStorageMem2DEXT,
                        buf.vlc_mem_obj, buf.vlc_tex, buf.dmabuf_fd, buf.size,
                        w, h, "VLC")) {
        if (buf.vlc_tex) { glDeleteTextures(1, &buf.vlc_tex); buf.vlc_tex = 0; }
        if (buf.vlc_mem_obj && glDeleteMemoryObjectsEXT) {
            glDeleteMemoryObjectsEXT(1, &buf.vlc_mem_obj); buf.vlc_mem_obj = 0;
        }
        if (buf.dmabuf_fd >= 0) { close(buf.dmabuf_fd); buf.dmabuf_fd = -1; }
        if (buf.bo) { gbm_bo_destroy(buf.bo); buf.bo = nullptr; }
        buf.stride = 0;
        buf.size = 0;
        return false;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Create FBO for VLC rendering
    glGenFramebuffers(1, &buf.vlc_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, buf.vlc_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buf.vlc_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DEBUG("[GLX] DMA-BUF FBO incomplete, status=0x%x", status);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (buf.vlc_fbo) { glDeleteFramebuffers(1, &buf.vlc_fbo); buf.vlc_fbo = 0; }
        if (buf.vlc_tex) { glDeleteTextures(1, &buf.vlc_tex); buf.vlc_tex = 0; }
        if (buf.vlc_mem_obj && glDeleteMemoryObjectsEXT) {
            glDeleteMemoryObjectsEXT(1, &buf.vlc_mem_obj); buf.vlc_mem_obj = 0;
        }
        if (buf.dmabuf_fd >= 0) { close(buf.dmabuf_fd); buf.dmabuf_fd = -1; }
        if (buf.bo) { gbm_bo_destroy(buf.bo); buf.bo = nullptr; }
        buf.stride = 0;
        buf.size = 0;
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    DEBUG("[GLX] DMA-BUF buffer created: vlc_tex=%u vlc_fbo=%u", buf.vlc_tex, buf.vlc_fbo);
    return true;
}

bool RenderAPI_OpenGLGLX::importDMABufToUnityContext(DMABufBuffer& buf, unsigned w, unsigned h)
{
    // Use raw GL function pointers to bypass Unity's GL wrapper.
    raw_glGenTextures(1, &buf.unity_tex);
    raw_glBindTexture(GL_TEXTURE_2D, buf.unity_tex);

    if (!importMemoryFd(glCreateMemoryObjectsEXT, glImportMemoryFdEXT, glDeleteMemoryObjectsEXT,
                        glMemoryObjectParameterivEXT, glTexStorageMem2DEXT,
                        buf.unity_mem_obj, buf.unity_tex, buf.dmabuf_fd, buf.size,
                        w, h, "Unity")) {
        if (buf.unity_tex) { raw_glDeleteTextures(1, &buf.unity_tex); buf.unity_tex = 0; }
        buf.unity_mem_obj = 0;
        return false;
    }

    raw_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    raw_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    raw_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    raw_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    raw_glBindTexture(GL_TEXTURE_2D, 0);

    DEBUG("[GLX] DMA-BUF imported to Unity context: unity_tex=%u", buf.unity_tex);
    return true;
}

void RenderAPI_OpenGLGLX::releaseDMABufResources()
{
    // Release VLC-side GL resources (need VLC context current)
    if (m_context) {
        GLXContext prev_ctx = glXGetCurrentContext();
        GLXDrawable prev_draw = glXGetCurrentDrawable();
        GLXDrawable prev_read = glXGetCurrentReadDrawable();
        makeCurrent(true);
        for (auto& buf : m_dmabuf_buffers) {
            if (buf.vlc_fbo) { glDeleteFramebuffers(1, &buf.vlc_fbo); buf.vlc_fbo = 0; }
            if (buf.vlc_tex) { glDeleteTextures(1, &buf.vlc_tex); buf.vlc_tex = 0; }
            if (buf.vlc_mem_obj && glDeleteMemoryObjectsEXT) {
                glDeleteMemoryObjectsEXT(1, &buf.vlc_mem_obj); buf.vlc_mem_obj = 0;
            }
        }
        glXMakeContextCurrent(m_display, prev_draw, prev_read, prev_ctx);
    }

    for (auto& buf : m_dmabuf_buffers) {
        buf.unity_tex = 0;
        buf.unity_mem_obj = 0;

        if (buf.dmabuf_fd >= 0) { close(buf.dmabuf_fd); buf.dmabuf_fd = -1; }
        if (buf.bo) { gbm_bo_destroy(buf.bo); buf.bo = nullptr; }
        buf.stride = 0;
        buf.size = 0;
    }

    m_unity_textures_imported = false;
    m_dmabuf_width = 0;
    m_dmabuf_height = 0;
    m_use_dmabuf = false;

    if (m_gbm_device) { gbm_device_destroy(m_gbm_device); m_gbm_device = nullptr; }
    if (m_drm_fd >= 0) { close(m_drm_fd); m_drm_fd = -1; }
}

// --- DMA-BUF VLC callbacks ---

bool RenderAPI_OpenGLGLX::dmabuf_setup(void** opaque,
                                        const libvlc_video_setup_device_cfg_t* cfg,
                                        libvlc_video_setup_device_info_t* out)
{
    (void)cfg; (void)out;
    DEBUG("[GLX] DMA-BUF output callback setup");
    auto* that = static_cast<RenderAPI_OpenGLGLX*>(*opaque);
    that->m_dmabuf_width = 0;
    that->m_dmabuf_height = 0;
    return true;
}

void RenderAPI_OpenGLGLX::dmabuf_cleanup(void* opaque)
{
    DEBUG("[GLX] DMA-BUF output callback cleanup");
    auto* that = static_cast<RenderAPI_OpenGLGLX*>(opaque);

    that->makeCurrent(true);
    for (auto& buf : that->m_dmabuf_buffers) {
        if (buf.vlc_fbo) { glDeleteFramebuffers(1, &buf.vlc_fbo); buf.vlc_fbo = 0; }
        if (buf.vlc_tex) { glDeleteTextures(1, &buf.vlc_tex); buf.vlc_tex = 0; }
        if (buf.vlc_mem_obj && that->glDeleteMemoryObjectsEXT) {
            that->glDeleteMemoryObjectsEXT(1, &buf.vlc_mem_obj); buf.vlc_mem_obj = 0;
        }
    }
    that->makeCurrent(false);
}

bool RenderAPI_OpenGLGLX::dmabuf_resize(void* opaque,
                                         const libvlc_video_render_cfg_t* cfg,
                                         libvlc_video_output_cfg_t* output)
{
    auto* that = static_cast<RenderAPI_OpenGLGLX*>(opaque);
    DEBUG("[GLX] DMA-BUF resize %ux%u", cfg->width, cfg->height);

    if (cfg->width != that->m_dmabuf_width || cfg->height != that->m_dmabuf_height) {
        // Release old resources
        for (auto& buf : that->m_dmabuf_buffers) {
            if (buf.vlc_fbo) { glDeleteFramebuffers(1, &buf.vlc_fbo); buf.vlc_fbo = 0; }
            if (buf.vlc_tex) { glDeleteTextures(1, &buf.vlc_tex); buf.vlc_tex = 0; }
            if (buf.vlc_mem_obj && that->glDeleteMemoryObjectsEXT) {
                that->glDeleteMemoryObjectsEXT(1, &buf.vlc_mem_obj); buf.vlc_mem_obj = 0;
            }
            if (buf.dmabuf_fd >= 0) { close(buf.dmabuf_fd); buf.dmabuf_fd = -1; }
            if (buf.bo) { gbm_bo_destroy(buf.bo); buf.bo = nullptr; }
            buf.unity_tex = 0;
            buf.unity_mem_obj = 0;
        }

        // Create new buffers
        for (size_t i = 0; i < kDMABufSlots; i++) {
            if (!that->createDMABufBuffer(that->m_dmabuf_buffers[i], cfg->width, cfg->height)) {
                DEBUG("[GLX] DMA-BUF buffer creation failed for slot %zu", i);
                return false;
            }
        }

        that->m_dmabuf_width = cfg->width;
        that->m_dmabuf_height = cfg->height;
        that->m_unity_textures_imported = false;
    }

    that->m_idx_render = 0;
    that->m_idx_swap = 1;
    that->m_idx_display = 2;

    glBindFramebuffer(GL_FRAMEBUFFER, that->m_dmabuf_buffers[that->m_idx_render].vlc_fbo);

    output->opengl_format = GL_RGBA;
    output->full_range = true;
    output->colorspace = libvlc_video_colorspace_BT709;
    output->primaries  = libvlc_video_primaries_BT709;
    output->transfer   = libvlc_video_transfer_func_SRGB;
    output->orientation = libvlc_video_orient_bottom_right;

    return true;
}

void RenderAPI_OpenGLGLX::dmabuf_swap(void* opaque)
{
    auto* that = static_cast<RenderAPI_OpenGLGLX*>(opaque);
    std::lock_guard<std::mutex> lock(that->m_dmabuf_lock);
    that->m_updated = true;

    // Flush GPU commands so Unity sees the rendered content via implicit DMA-BUF sync
    glFlush();

    std::swap(that->m_idx_swap, that->m_idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, that->m_dmabuf_buffers[that->m_idx_render].vlc_fbo);
}

void* RenderAPI_OpenGLGLX::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    if (!m_use_dmabuf)
        return RenderAPI_OpenGLBase::getVideoFrame(width, height, out_updated);

    std::lock_guard<std::mutex> lock(m_dmabuf_lock);

    if (out_updated)
        *out_updated = false;

    if (m_dmabuf_width == 0 || m_dmabuf_height == 0)
        return nullptr;

    // Textures not yet imported by render thread
    if (!m_unity_textures_imported)
        return nullptr;

    if (m_updated) {
        std::swap(m_idx_swap, m_idx_display);
        m_updated = false;
        if (out_updated)
            *out_updated = true;
    }

    return (void*)(size_t)m_dmabuf_buffers[m_idx_display].unity_tex;
}

#endif // SUPPORT_DMABUF
