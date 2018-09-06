#include "RenderAPI_OpenGLEGL.h"
#include "Log.h"

RenderAPI* CreateRenderAPI_OpenEGL(UnityGfxRenderer apiType)
{
	return new RenderAPI_OpenEGL(apiType);
}

RenderAPI_OpenEGL::RenderAPI_OpenEGL(UnityGfxRenderer apiType) :
    RenderAPI_OpenGLBase(apiType)
{
}


bool RenderAPI_OpenEGL::make_current(void* data, bool current)
{
    RenderAPI_OpenEGL* that = reinterpret_cast<RenderAPI_OpenEGL*>(data);
    //DEBUG("[EGL] make current %s disp=%p surf=%p ctx=%p", current ? "yes": "no", that->m_display, that->m_surface, that->m_context);
    EGLBoolean ret;
    if (current)
        ret = eglMakeCurrent(that->m_display, that->m_surface, that->m_surface, that->m_context);
    else
       ret = eglMakeCurrent(that->m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (ret ==  EGL_TRUE)
        return true;
    EGLint errcode = eglGetError();
    DEBUG("[EGL] make current failed: %x", errcode);
    return false;
}

void* RenderAPI_OpenEGL::get_proc_address(void* /*data*/, const char* procname)
{
    void* p = reinterpret_cast<void*>(eglGetProcAddress(procname));
    DEBUG("[EGL] get_proc_address %s %p", procname, p);
    return p;
}

void RenderAPI_OpenEGL::setVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[EGL] setVlcContext %p", this);
    libvlc_video_set_opengl_callbacks(mp,
        libvlc_gl_engine_gles2,
        setup, cleanup, resize, swap,
        make_current, get_proc_address,
        this);
}

EGLContext current_ctx;

void RenderAPI_OpenEGL::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	if (type == kUnityGfxDeviceEventInitialize) {
        DEBUG("[EGL] kUnityGfxDeviceEventInitialize");

        const EGLint config_attr[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
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

        const EGLint ctx_attr[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
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

        if(current_ctx == NULL){
            current_ctx = eglGetCurrentContext();
        }

        if(current_ctx == NULL) {
            if (eglGetError() != EGL_SUCCESS) {
                DEBUG("[EGL] eglGetCurrentContext() returned error %x", eglGetError());
                return;
            }
        }
        else {
            DEBUG("[EGL] eglGetCurrentContext() returned context %p", current_ctx);
        }

        m_surface = eglCreatePbufferSurface(m_display, config, surface_attr);
        if ( m_surface == EGL_NO_SURFACE || eglGetError() != EGL_SUCCESS ) {
            DEBUG("[EGL] eglCreatePbufferSurface() returned error %x", eglGetError());
            return;
        }


        m_context = eglCreateContext(m_display, config,  current_ctx, ctx_attr);
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
