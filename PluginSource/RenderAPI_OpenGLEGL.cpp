#include "RenderAPI_OpenGLEGL.h"
#include "Log.h"

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
        assert(eglGetCurrentContext() == EGL_NO_CONTEXT);
        ret = eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    }
    else
    {
        assert(eglGetCurrentContext() == m_context);
        // glClearColor(1, 0, 0, 1);
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

void RenderAPI_OpenEGL::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
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

        const EGLint ctx_attr[] = {
            EGL_CONTEXT_CLIENT_VERSION, egl_version,
            EGL_NONE
        };

        m_surface = eglCreatePbufferSurface(m_display, config, surface_attr);
        if ( m_surface == EGL_NO_SURFACE || eglGetError() != EGL_SUCCESS ) {
            DEBUG("[EGL] eglCreatePbufferSurface() returned error %x", eglGetError());
            return;
        }

        m_context = eglCreateContext(m_display, config,  unity_context, ctx_attr);
        if ( m_context == EGL_NO_CONTEXT ||  eglGetError() != EGL_SUCCESS ) {
            DEBUG("[EGL] eglCreateContext() returned error %x", eglGetError());
            return;
        }

        DEBUG("[EGL] kUnityGfxDeviceEventInitialize success disp=%p m_surf=%p m_ctx=%p", m_display, m_surface, m_context);
	} else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[EGL] kUnityGfxDeviceEventShutdown");
        eglDestroyContext(m_display, m_context);
        eglDestroySurface(m_display, m_surface);
	}
}
