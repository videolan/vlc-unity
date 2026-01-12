#include "RenderAPI_OpenGLEGL.h"
#include "Log.h"
#include <cassert>

#if defined(UNITY_ANDROID)
#define EGL_CONTEXT_OPENGL_NO_ERROR_KHR 0x31B3
#endif

EGLContext RenderAPI_OpenEGL::unity_context = EGL_NO_CONTEXT;

namespace {

bool staticMakeCurrent(void* data, bool current)
{
    auto that = static_cast<RenderAPI_OpenEGL*>(data);
    return that->makeCurrent(current);
}

}

RenderAPI* CreateRenderAPI_OpenEGL(UnityGfxRenderer apiType)
{
	return new RenderAPI_OpenEGL(apiType);
}

RenderAPI_OpenEGL::RenderAPI_OpenEGL(UnityGfxRenderer apiType) :
    RenderAPI_OpenGLBase(apiType)
{
}


bool RenderAPI_OpenEGL::makeCurrent(bool current)
{
    //DEBUG("[EGL] make current");
    //DEBUG("[EGL] make current %s disp=%p surf=%p ctx=%p", current ? "yes": "no", that->m_display, that->m_surface, that->m_context);
    EGLBoolean ret;
    if (current)
    {
        EGLContext currentCtx = eglGetCurrentContext();
        // If our context is already current, we're good
        if (currentCtx == m_context) {
            return true;
        }
        // If another context is current, we need to handle it
        // (VLC may have its own context current on this thread)
        ret = eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    }
    else
    {
        // Only release if our context is current
        if (eglGetCurrentContext() != m_context) {
            return true;  // Nothing to release
        }
        ret = eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (ret ==  EGL_TRUE)
        return true;
    EGLint errcode = eglGetError();
    DEBUG("[EGL] make current failed: %x", errcode);
    return false;
}

void* RenderAPI_OpenEGL::get_proc_address(void* /*data*/, const char* procname)
{
    void* p = reinterpret_cast<void*>(eglGetProcAddress(procname));
    //DEBUG("[EGL] get_proc_address %s %p", procname, p);
    return p;
}

void RenderAPI_OpenEGL::setVlcContext(libvlc_media_player_t *mp)
{
    if(unity_context == EGL_NO_CONTEXT) {
        DEBUG("OpenGL context has not been retrieved, aborting...");
        return;
    }

    m_mp = mp;

    DEBUG("[EGL] subscribing to opengl output callbacks %p", this);
    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_gles2,
        setup, cleanup, nullptr, resize, swap,
        staticMakeCurrent, get_proc_address, nullptr, nullptr, this);
}

// should only be called from the unity rendering thread
void RenderAPI_OpenEGL::retrieveOpenGLContext()
{
    if(unity_context == EGL_NO_CONTEXT) {
        unity_context = eglGetCurrentContext();
        if(unity_context == EGL_NO_CONTEXT) {
            DEBUG("[EGL] eglGetCurrentContext failed with %x", eglGetError());
        }
    }
}

void RenderAPI_OpenEGL::ensureCurrentContext()
{
    if(eglGetCurrentContext() == EGL_NO_CONTEXT) {
        makeCurrent(true);
    }
}

void RenderAPI_OpenEGL::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	(void)interfaces;
	if (type == kUnityGfxDeviceEventInitialize) {
        DEBUG("[EGL] Entering ProcessDeviceEvent with kUnityGfxDeviceEventInitialize");

        if(unity_context == EGL_NO_CONTEXT) {
            DEBUG("[EGL] Failed to retrieve OpenGL Context... aborting.");
            return;
        }

        const EGLint config_attr[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
            EGL_RED_SIZE,   8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8,
            EGL_ALPHA_SIZE, 8,     // if you need the alpha channel
            EGL_NONE
        };

        const EGLint surface_attr[] = {
            EGL_WIDTH, 2,
            EGL_HEIGHT, 2,
            EGL_NONE
        };

        EGLConfig config;
        EGLint num_configs;

        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

        if (m_display == EGL_NO_DISPLAY || eglGetError() != EGL_SUCCESS) {
            DEBUG("[EGL] eglGetCurrentDisplay() returned error %x", eglGetError());
            return;
        }

        if (!eglInitialize(m_display, nullptr, nullptr)) {
            DEBUG("[EGL] eglInitialize() returned error %x", eglGetError());
            return;
        }

        if (!eglChooseConfig(m_display, config_attr, &config, 1, &num_configs) || eglGetError() != EGL_SUCCESS) {
            DEBUG("[EGL] eglGetConfigAttrib() returned error %x", eglGetError());
            return;
        }

        EGLint egl_version;
        if (!eglQueryContext(m_display, unity_context, EGL_CONTEXT_CLIENT_VERSION, &egl_version)) {
            DEBUG("[EGL] failed to retrieve EGL_CONTEXT_CLIENT_VERSION");
            DEBUG("[EGL] eglQueryContext() returned error %x", eglGetError());
            return;
        }

        m_surface = eglCreatePbufferSurface(m_display, config, surface_attr);
        if ( m_surface == EGL_NO_SURFACE || eglGetError() != EGL_SUCCESS ) {
            DEBUG("[EGL] eglCreatePbufferSurface() returned error %x", eglGetError());
            return;
        }

        // Regular context creation
        EGLint ctx_attr[3];
        ctx_attr[0] = EGL_CONTEXT_CLIENT_VERSION;
        ctx_attr[1] = egl_version;
        ctx_attr[2] = EGL_NONE;

        m_context = eglCreateContext(m_display, config, unity_context, ctx_attr);
        if (m_context != EGL_NO_CONTEXT && eglGetError() == EGL_SUCCESS) {
            DEBUG("[EGL] kUnityGfxDeviceEventInitialize success disp=%p m_surf=%p m_ctx=%p", m_display, m_surface, m_context);
            return;
        }
        DEBUG("[EGL] eglCreateContext() failed for regular context creation, error %x", eglGetError());

#if defined(UNITY_ANDROID)
        // Low overhead mode context creation
        const EGLint low_overhead_attr_size = 5;
        EGLint ctx_attr_low_overhead[low_overhead_attr_size];
        const char* extensions = eglQueryString(m_display, EGL_EXTENSIONS);
        if (extensions == NULL) {
            DEBUG("[EGL] eglQueryString(EGL_EXTENSIONS) not found, required for low overhead mode");
            return;
        }

        ctx_attr_low_overhead[0] = EGL_CONTEXT_CLIENT_VERSION;
        ctx_attr_low_overhead[1] = egl_version;
        ctx_attr_low_overhead[2] = EGL_CONTEXT_OPENGL_NO_ERROR_KHR;
        ctx_attr_low_overhead[3] = EGL_TRUE;
        ctx_attr_low_overhead[4] = EGL_NONE;

        m_context = eglCreateContext(m_display, config, unity_context, ctx_attr_low_overhead);
        if (m_context != EGL_NO_CONTEXT || eglGetError() == EGL_SUCCESS) {
            DEBUG("[EGL] kUnityGfxDeviceEventInitialize success disp=%p m_surf=%p m_ctx=%p", m_display, m_surface, m_context);
        }
        else{
            DEBUG("[EGL] eglCreateContext() failed for low overhead context, error %x", eglGetError());
        }
#endif
	} else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[EGL] kUnityGfxDeviceEventShutdown");
        eglDestroyContext(m_display, m_context);
        eglDestroySurface(m_display, m_surface);
	}
}
