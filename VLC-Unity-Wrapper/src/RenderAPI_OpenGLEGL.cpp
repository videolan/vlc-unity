#include "RenderAPI_OpenGLBase.h"
#include "PlatformBase.h"
#include "Log.h"
#include <EGL/egl.h>

#if UNITY_ANDROID
#include <dlfcn.h>

#include <jni.h>

static JNIEnv* jni_env = 0;
static void *handle;
typedef jint (*JNI_OnLoad_pf)(JavaVM *, void*);

extern "C" int VLCJNI_OnLoad(JavaVM *, JNIEnv*);

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    jni_env = 0;
    if (vm->GetEnv(reinterpret_cast<void**>(&jni_env), JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    vm->AttachCurrentThread(&jni_env, 0);

    handle = dlopen("libvlcjni.so", RTLD_LAZY);
    if (!handle)
    {
        DEBUG("could not link libvlcjni.so");
        return -1;
    }


    if ( VLCJNI_OnLoad(vm, jni_env) != 0 )
    {
        DEBUG("VLCJNI_OnLoad failed");
    }

    JNI_OnLoad_pf load;
    load = (JNI_OnLoad_pf) dlsym(handle, "JNI_OnLoad");
    if (!load || load(vm, jni_env) < 0)
    {
        if (!load)
            DEBUG("could not find VLCJNI_OnLoad");
        else
            DEBUG("VLC JNI_OnLoad failed");
        return -1;
    }

    DEBUG("[EGL] initialize jni env %p", jni_env);
    return JNI_VERSION_1_6;
}



#endif

class RenderAPI_OpenEGL : public RenderAPI_OpenGLBase
{
public:
	RenderAPI_OpenEGL(UnityGfxRenderer apiType);
	virtual ~RenderAPI_OpenEGL() { }

    virtual void setVlcContext(libvlc_media_player_t *mp, void* textureHandle) override;
	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;

    static bool  make_current(void* data, bool current);
    static void* get_proc_address(void* /*data*/, const char* current);

private:
	UnityGfxRenderer m_APIType;

    EGLDisplay m_display;
    EGLSurface m_surface;
    EGLContext m_context;

#if UNITY_ANDROID
private:
    void* createWindowSurface();

    void* m_awindow;
#endif
};

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

#if UNITY_ANDROID
void* RenderAPI_OpenEGL::createWindowSurface()
{
    jclass cls_JavaClass = jni_env->FindClass("org/videolan/libvlc/AWindow");         // find class definition
    jmethodID mid_JavaClass = jni_env->GetMethodID (cls_JavaClass, "<init>", "(Lorg/videolan/libvlc/AWindow$SurfaceCallback;)V");      // find constructor method
    jobject obj_JavaClass = jni_env->NewObject(cls_JavaClass, mid_JavaClass, nullptr);     // create object instance
    return jni_env->NewGlobalRef(obj_JavaClass);                      // return object with a global reference
}
#endif

void RenderAPI_OpenEGL::setVlcContext(libvlc_media_player_t *mp, void* textureHandle)
{
    DEBUG("[EGL] setVlcContext %p", this);
#if UNITY_ANDROID
    m_awindow = createWindowSurface();
    DEBUG("[EGL] createWindowSurface %p", m_awindow);
    libvlc_media_player_set_android_context(mp, m_awindow);
#endif
    libvlc_video_set_opengl_callbacks(mp, create_fbo, destroy_fbo, make_current, get_proc_address, render_fbo, this);
}

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
            EGL_CONTEXT_CLIENT_VERSION, 3,
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

        EGLContext current_ctx = eglGetCurrentContext();
        if (eglGetError() != EGL_SUCCESS) {
            DEBUG("[EGL] eglGetCurrentContext() returned error %x", eglGetError());
            return;
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
