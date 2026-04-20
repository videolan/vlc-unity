#include "RenderAPI_OpenGLLinuxEGL.h"
#include "Log.h"
#include <cassert>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#endif
#ifndef GL_DEDICATED_MEMORY_OBJECT_EXT
#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#endif

namespace {

bool staticMakeCurrent(void* data, bool current)
{
    auto that = static_cast<RenderAPI_OpenGLLinuxEGL*>(data);
    return that->makeCurrent(current);
}

} // namespace

bool RenderAPI_OpenGLLinuxEGL::m_unity_context_ready = false;

RenderAPI* CreateRenderAPI_OpenGLLinuxEGL(UnityGfxRenderer apiType)
{
    return new RenderAPI_OpenGLLinuxEGL(apiType);
}

RenderAPI_OpenGLLinuxEGL::RenderAPI_OpenGLLinuxEGL(UnityGfxRenderer apiType) :
    RenderAPI_OpenEGL(apiType)
{
    // Parent class doesn't initialize these, causing garbage values
    // that bypass safety checks if ProcessDeviceEvent fails
    m_display = EGL_NO_DISPLAY;
    m_surface = EGL_NO_SURFACE;
    m_context = EGL_NO_CONTEXT;
}

// ---------------------------------------------------------------------------
// proc address: try EGL first, fall back to GLX for core GL functions
// ---------------------------------------------------------------------------

void* RenderAPI_OpenGLLinuxEGL::get_proc_address_desktop(void* /*data*/, const char* procname)
{
    void* p = reinterpret_cast<void*>(eglGetProcAddress(procname));
    if (!p)
        p = reinterpret_cast<void*>(glXGetProcAddressARB(
            reinterpret_cast<const GLubyte*>(procname)));
    return p;
}

// ---------------------------------------------------------------------------
// retrieveOpenGLContext — detect Unity's GL context (GLX or EGL)
// ---------------------------------------------------------------------------

void RenderAPI_OpenGLLinuxEGL::retrieveOpenGLContext()
{
    if (m_unity_context_ready)
        return;

    // Check if Unity exposes an EGL context (native Wayland)
    EGLContext egl_ctx = eglGetCurrentContext();
    if (egl_ctx != EGL_NO_CONTEXT) {
        DEBUG("[EGL-Linux] Unity has an EGL context %p", egl_ctx);
        m_unity_context_ready = true;
        return;
    }

    // Check if Unity exposes a GLX context (XWayland)
    GLXContext glx_ctx = glXGetCurrentContext();
    if (glx_ctx != nullptr) {
        DEBUG("[EGL-Linux] Unity has a GLX context %p (XWayland)", glx_ctx);
        m_unity_context_ready = true;
        return;
    }

    DEBUG("[EGL-Linux] no Unity GL context detected yet");
}

// ---------------------------------------------------------------------------
// ProcessDeviceEvent — create GBM-backed EGL context with desktop GL
// ---------------------------------------------------------------------------

void RenderAPI_OpenGLLinuxEGL::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    (void)interfaces;

    if (type == kUnityGfxDeviceEventInitialize) {
        DEBUG("[EGL-Linux] ProcessDeviceEvent Initialize");

        if (m_context != EGL_NO_CONTEXT) {
            return;
        }

        if (!m_unity_context_ready) {
            DEBUG("[EGL-Linux] Unity GL context not yet available, aborting.");
            return;
        }

        if (!initDRMAndGBM()) {
            DEBUG("[EGL-Linux] DRM/GBM init failed");
            return;
        }

        // Create EGL display on our GBM device
        typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC_)(EGLenum, void*, const EGLint*);
        auto eglGetPlatformDisplayEXT_ =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC_>(
                eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (eglGetPlatformDisplayEXT_) {
            m_display = eglGetPlatformDisplayEXT_(EGL_PLATFORM_GBM_KHR,
                                                   m_gbm_device, nullptr);
        }
        if (m_display == EGL_NO_DISPLAY) {
            m_display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(m_gbm_device));
        }
        if (m_display == EGL_NO_DISPLAY) {
            DEBUG("[EGL-Linux] eglGetDisplay failed: 0x%x", eglGetError());
            return;
        }

        if (!eglInitialize(m_display, nullptr, nullptr)) {
            DEBUG("[EGL-Linux] eglInitialize failed: 0x%x", eglGetError());
            m_display = EGL_NO_DISPLAY;
            return;
        }

        // Log EGL version and client APIs
        {
            const char* egl_vendor = eglQueryString(m_display, EGL_VENDOR);
            const char* egl_version = eglQueryString(m_display, EGL_VERSION);
            const char* egl_apis = eglQueryString(m_display, EGL_CLIENT_APIS);
            DEBUG("[EGL-Linux] EGL vendor=%s version=%s apis=%s",
                  egl_vendor ? egl_vendor : "?",
                  egl_version ? egl_version : "?",
                  egl_apis ? egl_apis : "?");
        }

        // Bind desktop OpenGL API (not GLES)
        if (!eglBindAPI(EGL_OPENGL_API)) {
            DEBUG("[EGL-Linux] eglBindAPI(EGL_OPENGL_API) failed: 0x%x", eglGetError());
            return;
        }
        DEBUG("[EGL-Linux] eglBindAPI(EGL_OPENGL_API) succeeded");

        // Choose an EGL config — surfaceless (we render to DMA-BUF FBOs)
        // GBM displays typically don't support pbuffers
        EGLConfig config;
        EGLint num_configs = 0;
        bool config_found = false;

        // Try 1: RGBA8 with no surface type constraint
        {
            const EGLint attr[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                EGL_SURFACE_TYPE,    0,
                EGL_RED_SIZE,   8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE,  8,
                EGL_ALPHA_SIZE, 8,
                EGL_NONE
            };
            if (eglChooseConfig(m_display, attr, &config, 1, &num_configs) && num_configs > 0) {
                DEBUG("[EGL-Linux] config found (surfaceless RGBA8): %d configs", num_configs);
                config_found = true;
            }
        }

        // Try 2: minimal constraints
        if (!config_found) {
            const EGLint attr[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                EGL_NONE
            };
            if (eglChooseConfig(m_display, attr, &config, 1, &num_configs) && num_configs > 0) {
                DEBUG("[EGL-Linux] config found (minimal GL): %d configs", num_configs);
                config_found = true;
            }
        }

        // Try 3: any config at all (debug)
        if (!config_found) {
            EGLint total = 0;
            eglGetConfigs(m_display, nullptr, 0, &total);
            DEBUG("[EGL-Linux] no GL configs found, total configs on display: %d", total);
            if (total > 0) {
                // Try GLES as last resort
                eglBindAPI(EGL_OPENGL_ES_API);
                const EGLint attr[] = {
                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                    EGL_NONE
                };
                if (eglChooseConfig(m_display, attr, &config, 1, &num_configs) && num_configs > 0) {
                    DEBUG("[EGL-Linux] WARNING: only GLES configs available, not desktop GL");
                }
            }
            DEBUG("[EGL-Linux] eglChooseConfig failed, cannot create EGL context");
            return;
        }

        // Surfaceless context — we only render to FBOs backed by DMA-BUF
        m_surface = EGL_NO_SURFACE;

        // Create a standalone GL context (no sharing — we use DMA-BUF instead)
        // Try GL 4.5 core, then 3.3 core
        static const int gl_versions[][2] = { {4, 5}, {3, 3} };
        for (auto& ver : gl_versions) {
            const EGLint ctx_attr[] = {
                EGL_CONTEXT_MAJOR_VERSION, ver[0],
                EGL_CONTEXT_MINOR_VERSION, ver[1],
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                EGL_NONE
            };
            m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, ctx_attr);
            if (m_context != EGL_NO_CONTEXT) {
                DEBUG("[EGL-Linux] created GL %d.%d core context", ver[0], ver[1]);
                break;
            }
        }

        // Fallback: compatibility profile
        if (m_context == EGL_NO_CONTEXT) {
            for (auto& ver : gl_versions) {
                const EGLint ctx_attr[] = {
                    EGL_CONTEXT_MAJOR_VERSION, ver[0],
                    EGL_CONTEXT_MINOR_VERSION, ver[1],
                    EGL_NONE
                };
                m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, ctx_attr);
                if (m_context != EGL_NO_CONTEXT) {
                    DEBUG("[EGL-Linux] created GL %d.%d compat context", ver[0], ver[1]);
                    break;
                }
            }
        }

        if (m_context == EGL_NO_CONTEXT) {
            DEBUG("[EGL-Linux] all eglCreateContext attempts failed: 0x%x", eglGetError());
            return;
        }

        // Verify we got the right GPU, then load extensions
        makeCurrent(true);
        {
            const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            const char* gl_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            DEBUG("[EGL-Linux] GL renderer=%s version=%s vendor=%s",
                  gl_renderer ? gl_renderer : "?",
                  gl_version ? gl_version : "?",
                  gl_vendor ? gl_vendor : "?");
        }
        if (!loadMemoryObjectExtensions()) {
            DEBUG("[EGL-Linux] GL_EXT_memory_object_fd not available — cannot share textures");
            makeCurrent(false);
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
            return;
        }
        makeCurrent(false);

        DEBUG("[EGL-Linux] init success: display=%p surface=%p context=%p gbm=%p",
              m_display, m_surface, m_context, m_gbm_device);
        libvlc_media_player_t* pending = m_pending_mp.exchange(nullptr);
        if (pending)
            setVlcContext(pending);

    } else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[EGL-Linux] ProcessDeviceEvent Shutdown");
        releaseResources();
    }
}

// ---------------------------------------------------------------------------
// setVlcContext — register DMA-BUF output callbacks with desktop GL engine
// ---------------------------------------------------------------------------

void RenderAPI_OpenGLLinuxEGL::setVlcContext(libvlc_media_player_t *mp)
{
    if (m_context == EGL_NO_CONTEXT) {
        m_pending_mp.store(mp);
        DEBUG("[EGL-Linux] no EGL context, deferring setVlcContext");
        return;
    }
    m_pending_mp.store(nullptr);

    DEBUG("[EGL-Linux] subscribing to DMA-BUF opengl output callbacks %p", this);
    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_opengl,
        dmabuf_setup, dmabuf_cleanup, nullptr, dmabuf_resize, dmabuf_swap,
        staticMakeCurrent, get_proc_address_desktop, nullptr, nullptr, this);
}

void RenderAPI_OpenGLLinuxEGL::unsetVlcContext(libvlc_media_player_t *mp)
{
    libvlc_media_player_t* expected = mp;
    if (m_pending_mp.compare_exchange_strong(expected, nullptr))
        return; // Cancelled pending setVlcContext before it was promoted
    // Callbacks were already registered — disable them
    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_disable,
        nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// DRM/GBM initialization
// ---------------------------------------------------------------------------

bool RenderAPI_OpenGLLinuxEGL::initDRMAndGBM()
{
    DIR* dir = opendir("/dev/dri");
    if (!dir) {
        DEBUG("[EGL-Linux] cannot open /dev/dri");
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "renderD", 7) == 0) {
            char path[280];
            snprintf(path, sizeof(path), "/dev/dri/%.255s", entry->d_name);
            m_drm_fd = open(path, O_RDWR | O_CLOEXEC);
            if (m_drm_fd >= 0) {
                DEBUG("[EGL-Linux] opened DRM render node %s (fd=%d)", path, m_drm_fd);
                break;
            }
        }
    }
    closedir(dir);

    if (m_drm_fd < 0) {
        DEBUG("[EGL-Linux] no DRM render node found");
        return false;
    }

    m_gbm_device = gbm_create_device(m_drm_fd);
    if (!m_gbm_device) {
        DEBUG("[EGL-Linux] gbm_create_device failed");
        close(m_drm_fd);
        m_drm_fd = -1;
        return false;
    }

    DEBUG("[EGL-Linux] GBM device created");
    return true;
}

// ---------------------------------------------------------------------------
// GL_EXT_memory_object_fd extension loading
// ---------------------------------------------------------------------------

bool RenderAPI_OpenGLLinuxEGL::loadMemoryObjectExtensions()
{
    // In core profile (GL 3.2+), glGetString(GL_EXTENSIONS) is deprecated
    // and may return NULL. Must use glGetStringi(GL_EXTENSIONS, i) instead.
    bool has_memory_object = false;
    bool has_memory_object_fd = false;

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions) {
        has_memory_object = strstr(extensions, "GL_EXT_memory_object") != nullptr;
        has_memory_object_fd = strstr(extensions, "GL_EXT_memory_object_fd") != nullptr;
    } else {
        // Core profile: enumerate extensions individually
        typedef const GLubyte* (*PFNGLGETSTRINGIPROC)(GLenum, GLuint);
        auto glGetStringi_ = reinterpret_cast<PFNGLGETSTRINGIPROC>(
            eglGetProcAddress("glGetStringi"));
        if (glGetStringi_) {
            GLint num_ext = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &num_ext);
            DEBUG("[EGL-Linux] enumerating %d GL extensions (core profile)", num_ext);
            for (GLint i = 0; i < num_ext; i++) {
                const char* ext = reinterpret_cast<const char*>(glGetStringi_(GL_EXTENSIONS, i));
                if (ext) {
                    if (strcmp(ext, "GL_EXT_memory_object") == 0) has_memory_object = true;
                    if (strcmp(ext, "GL_EXT_memory_object_fd") == 0) has_memory_object_fd = true;
                }
            }
        }
    }

    if (!has_memory_object || !has_memory_object_fd) {
        DEBUG("[EGL-Linux] GL_EXT_memory_object=%d GL_EXT_memory_object_fd=%d — not available",
              has_memory_object, has_memory_object_fd);
        return false;
    }

    auto loadProc = [](const char* name) -> void* {
        void* p = reinterpret_cast<void*>(eglGetProcAddress(name));
        if (!p)
            p = reinterpret_cast<void*>(glXGetProcAddressARB(
                reinterpret_cast<const GLubyte*>(name)));
        return p;
    };

    glCreateMemoryObjectsEXT = reinterpret_cast<PFNGLCREATEMEMORYOBJECTSEXTPROC>(
        loadProc("glCreateMemoryObjectsEXT"));
    glTexStorageMem2DEXT = reinterpret_cast<PFNGLTEXSTORAGEMEM2DEXTPROC>(
        loadProc("glTexStorageMem2DEXT"));
    glImportMemoryFdEXT = reinterpret_cast<PFNGLIMPORTMEMORYFDEXTPROC>(
        loadProc("glImportMemoryFdEXT"));
    glDeleteMemoryObjectsEXT = reinterpret_cast<PFNGLDELETEMEMORYOBJECTSEXTPROC>(
        loadProc("glDeleteMemoryObjectsEXT"));
    glMemoryObjectParameterivEXT = reinterpret_cast<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_>(
        loadProc("glMemoryObjectParameterivEXT"));

    if (!glCreateMemoryObjectsEXT || !glTexStorageMem2DEXT ||
        !glImportMemoryFdEXT || !glDeleteMemoryObjectsEXT) {
        DEBUG("[EGL-Linux] failed to load GL_EXT_memory_object_fd functions");
        return false;
    }

    // Load raw GL functions (bypass Unity's GL wrapper for Unity-context ops)
    raw_glGenTextures = reinterpret_cast<PFNGLGENTEXTURESPROC_RAW>(loadProc("glGenTextures"));
    raw_glBindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC_RAW>(loadProc("glBindTexture"));
    raw_glTexParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC_RAW>(loadProc("glTexParameteri"));
    raw_glDeleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC_RAW>(loadProc("glDeleteTextures"));

    if (!raw_glGenTextures || !raw_glBindTexture || !raw_glTexParameteri) {
        DEBUG("[EGL-Linux] failed to load raw GL function pointers");
        return false;
    }

    DEBUG("[EGL-Linux] GL_EXT_memory_object_fd extensions loaded");
    return true;
}

// ---------------------------------------------------------------------------
// DMA-BUF buffer creation and import
// ---------------------------------------------------------------------------

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

    if (glMemoryObjectParameterivEXT) {
        GLint dedicated = GL_TRUE;
        glMemoryObjectParameterivEXT(mem_obj, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
    }

    // dup() because glImportMemoryFdEXT takes ownership of the fd
    int import_fd = dup(dmabuf_fd);
    if (import_fd < 0) {
        DEBUG("[EGL-Linux] dup(dmabuf_fd) failed for %s import", label);
        if (glDeleteMemoryObjectsEXT && mem_obj) {
            glDeleteMemoryObjectsEXT(1, &mem_obj);
            mem_obj = 0;
        }
        return false;
    }

    glImportMemoryFdEXT(mem_obj, size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, import_fd);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG("[EGL-Linux] glImportMemoryFdEXT failed for %s, GL error=0x%x", label, err);
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
        DEBUG("[EGL-Linux] glTexStorageMem2DEXT failed for %s, GL error=0x%x (size=%lu, %ux%u)",
              label, err, (unsigned long)size, w, h);
        if (glDeleteMemoryObjectsEXT && mem_obj) {
            glDeleteMemoryObjectsEXT(1, &mem_obj);
            mem_obj = 0;
        }
        return false;
    }

    DEBUG("[EGL-Linux] memory object imported for %s: tex=%u mem=%u size=%lu",
          label, tex, mem_obj, (unsigned long)size);
    return true;
}

bool RenderAPI_OpenGLLinuxEGL::createDMABufBuffer(DMABufBuffer& buf, unsigned w, unsigned h)
{
    buf.bo = gbm_bo_create(m_gbm_device, w, h, GBM_FORMAT_ABGR8888,
                           GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!buf.bo) {
        DEBUG("[EGL-Linux] gbm_bo_create failed %ux%u", w, h);
        return false;
    }

    buf.dmabuf_fd = gbm_bo_get_fd(buf.bo);
    if (buf.dmabuf_fd < 0) {
        DEBUG("[EGL-Linux] gbm_bo_get_fd failed");
        gbm_bo_destroy(buf.bo);
        buf.bo = nullptr;
        return false;
    }

    buf.stride = gbm_bo_get_stride(buf.bo);

    off_t real_size = lseek(buf.dmabuf_fd, 0, SEEK_END);
    lseek(buf.dmabuf_fd, 0, SEEK_SET);
    if (real_size <= 0) {
        buf.size = (uint64_t)buf.stride * h;
        DEBUG("[EGL-Linux] lseek failed, using stride*height=%lu", (unsigned long)buf.size);
    } else {
        buf.size = (uint64_t)real_size;
    }

    DEBUG("[EGL-Linux] DMA-BUF: fd=%d stride=%u size=%lu %ux%u",
          buf.dmabuf_fd, buf.stride, (unsigned long)buf.size, w, h);

    // Import into VLC's EGL/GL context
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

    glGenFramebuffers(1, &buf.vlc_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, buf.vlc_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buf.vlc_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DEBUG("[EGL-Linux] DMA-BUF FBO incomplete, status=0x%x", status);
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

    DEBUG("[EGL-Linux] DMA-BUF buffer created: vlc_tex=%u vlc_fbo=%u", buf.vlc_tex, buf.vlc_fbo);
    return true;
}

bool RenderAPI_OpenGLLinuxEGL::importDMABufToUnityContext(DMABufBuffer& buf, unsigned w, unsigned h)
{
    // Verify Unity's GL context is current
    GLXContext glx_ctx = glXGetCurrentContext();
    EGLContext egl_ctx = eglGetCurrentContext();
    DEBUG("[EGL-Linux] importDMABufToUnityContext: GLX ctx=%p EGL ctx=%p", glx_ctx, egl_ctx);

    if (!glx_ctx && egl_ctx == EGL_NO_CONTEXT) {
        DEBUG("[EGL-Linux] no GL context current on Unity thread!");
        return false;
    }

    // Check Unity context's GL renderer and extension support
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    DEBUG("[EGL-Linux] Unity GL renderer: %s", renderer ? renderer : "NULL");

    raw_glGenTextures(1, &buf.unity_tex);
    DEBUG("[EGL-Linux] raw_glGenTextures produced tex=%u (glError=0x%x)",
          buf.unity_tex, glGetError());
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

    DEBUG("[EGL-Linux] DMA-BUF imported to Unity context: unity_tex=%u", buf.unity_tex);
    return true;
}

// ---------------------------------------------------------------------------
// Resource cleanup
// ---------------------------------------------------------------------------

void RenderAPI_OpenGLLinuxEGL::releaseResources()
{
    if (m_context != EGL_NO_CONTEXT) {
        makeCurrent(true);
        for (auto& buf : m_dmabuf_buffers) {
            if (buf.vlc_fbo) { glDeleteFramebuffers(1, &buf.vlc_fbo); buf.vlc_fbo = 0; }
            if (buf.vlc_tex) { glDeleteTextures(1, &buf.vlc_tex); buf.vlc_tex = 0; }
            if (buf.vlc_mem_obj && glDeleteMemoryObjectsEXT) {
                glDeleteMemoryObjectsEXT(1, &buf.vlc_mem_obj); buf.vlc_mem_obj = 0;
            }
        }
        makeCurrent(false);
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

    if (m_context != EGL_NO_CONTEXT) {
        eglDestroyContext(m_display, m_context);
        m_context = EGL_NO_CONTEXT;
    }
    if (m_surface != EGL_NO_SURFACE) {
        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
    }
    if (m_display != EGL_NO_DISPLAY) {
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
    }
    if (m_gbm_device) { gbm_device_destroy(m_gbm_device); m_gbm_device = nullptr; }
    if (m_drm_fd >= 0) { close(m_drm_fd); m_drm_fd = -1; }
}

// ---------------------------------------------------------------------------
// DMA-BUF VLC callbacks
// ---------------------------------------------------------------------------

bool RenderAPI_OpenGLLinuxEGL::dmabuf_setup(void** opaque,
                                             const libvlc_video_setup_device_cfg_t* cfg,
                                             libvlc_video_setup_device_info_t* out)
{
    (void)cfg; (void)out;
    DEBUG("[EGL-Linux] DMA-BUF output callback setup");
    auto* that = static_cast<RenderAPI_OpenGLLinuxEGL*>(*opaque);
    that->m_dmabuf_width = 0;
    that->m_dmabuf_height = 0;
    return true;
}

void RenderAPI_OpenGLLinuxEGL::dmabuf_cleanup(void* opaque)
{
    DEBUG("[EGL-Linux] DMA-BUF output callback cleanup");
    auto* that = static_cast<RenderAPI_OpenGLLinuxEGL*>(opaque);

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

bool RenderAPI_OpenGLLinuxEGL::dmabuf_resize(void* opaque,
                                              const libvlc_video_render_cfg_t* cfg,
                                              libvlc_video_output_cfg_t* output)
{
    auto* that = static_cast<RenderAPI_OpenGLLinuxEGL*>(opaque);
    DEBUG("[EGL-Linux] DMA-BUF resize %ux%u", cfg->width, cfg->height);

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

        for (size_t i = 0; i < kDMABufSlots; i++) {
            if (!that->createDMABufBuffer(that->m_dmabuf_buffers[i], cfg->width, cfg->height)) {
                DEBUG("[EGL-Linux] DMA-BUF buffer creation failed for slot %zu", i);
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

void RenderAPI_OpenGLLinuxEGL::dmabuf_swap(void* opaque)
{
    auto* that = static_cast<RenderAPI_OpenGLLinuxEGL*>(opaque);
    std::lock_guard<std::mutex> lock(that->m_dmabuf_lock);
    that->m_updated = true;

    glFlush();

    std::swap(that->m_idx_swap, that->m_idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, that->m_dmabuf_buffers[that->m_idx_render].vlc_fbo);
}

// ---------------------------------------------------------------------------
// performRenderThreadWork — called from OnRenderEvent on the render thread
// where Unity's GL context IS current.
// Either import DMA-BUF textures directly, or do CPU readback if cross-device.
// ---------------------------------------------------------------------------

void RenderAPI_OpenGLLinuxEGL::performRenderThreadWork()
{
    std::lock_guard<std::mutex> lock(m_dmabuf_lock);

    if (m_unity_textures_imported || m_dmabuf_width == 0 || m_dmabuf_height == 0)
        return;

    DEBUG("[EGL-Linux] importing DMA-BUF textures into Unity context (render thread)");
    for (size_t i = 0; i < kDMABufSlots; i++) {
        if (!importDMABufToUnityContext(m_dmabuf_buffers[i], m_dmabuf_width, m_dmabuf_height)) {
            DEBUG("[EGL-Linux] failed to import DMA-BUF buffer %zu into Unity context", i);
            return;
        }
    }
    m_unity_textures_imported = true;
    DEBUG("[EGL-Linux] all DMA-BUF textures imported into Unity context");
}

// ---------------------------------------------------------------------------
// getVideoFrame — return texture handle
// ---------------------------------------------------------------------------

void* RenderAPI_OpenGLLinuxEGL::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    (void)width; (void)height;
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
