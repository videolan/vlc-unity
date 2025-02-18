#include <dlfcn.h>
#include <jni.h>
#include "Log.h"
#include "RenderAPI_OpenGLEGL.h"

static JNIEnv* jni_env = 0;
static void *handle;
typedef jint (*JNI_OnLoad_pf)(JavaVM *, void*);
typedef void (*JNI_OnUnload_pf)(JavaVM *, void*);

// extern "C" int VLCJNI_OnLoad(JavaVM *, JNIEnv*);
// extern "C" void VLCJNI_OnUnload(JavaVM *, JNIEnv *);

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    DEBUG("ENTERED RENDERAPI_ANDROID.CPP -> JNI_ONLOAD");

    //if (vm->GetEnv(reinterpret_cast<void**>(&jni_env), JNI_VERSION_1_6) != JNI_OK) {
    //    return -1;
    //}
    vm->AttachCurrentThread(&jni_env, 0);

    // if ( VLCJNI_OnLoad(vm, jni_env) != 0 )
    // {
    //     DEBUG("VLCJNI_OnLoad failed");
    //     return -1;
    // }

    handle = dlopen("libvlc.so", RTLD_LAZY);
    if (!handle)
    {
        DEBUG("could not link libvlc.so");
        return -1;
    }

    DEBUG("(JNI_OnLoad_pf) dlsym(handle, JNI_OnLoad);");

    JNI_OnLoad_pf load;
    load = (JNI_OnLoad_pf) dlsym(handle, "JNI_OnLoad");
    if (!load || load(vm, jni_env) < 0)
    {
        if (!load)
            DEBUG("could not find VLC JNI_OnLoad");
        else
            DEBUG("VLC JNI_OnLoad failed");
        return -1;
    }


    DEBUG("[Android] initialize jni env %p", jni_env);
    DEBUG("Exiting...");

    return JNI_VERSION_1_6;
}


void JNI_OnUnload(JavaVM *vm, void *reserved)
{
    DEBUG("[Android] unload jni env %p", jni_env);

    JNI_OnLoad_pf unload;

    unload = (JNI_OnLoad_pf) dlsym(handle, "JNI_OnUnload");
    if (unload)
        unload(vm, jni_env);
    else
        DEBUG("could not find VLC JNI_OnUnload");
    dlclose(handle);

    // VLCJNI_OnUnload(vm, jni_env);

    vm->DetachCurrentThread();
}


class RenderAPI_Android : public RenderAPI_OpenEGL
{
public:
	RenderAPI_Android(UnityGfxRenderer apiType);
	virtual ~RenderAPI_Android();

    virtual void setVlcContext(libvlc_media_player_t *mp) override;

private:
    jobject createWindowSurface();
    void destroyWindowSurface(jobject);
    jobject m_awindow = nullptr;
};

RenderAPI* CreateRenderAPI_Android(UnityGfxRenderer apiType)
{
	return new RenderAPI_Android(apiType);
}

RenderAPI_Android::RenderAPI_Android(UnityGfxRenderer apiType) :
    RenderAPI_OpenEGL(apiType)
{

}

RenderAPI_Android::~RenderAPI_Android()
{
    if (m_awindow)
        destroyWindowSurface(m_awindow);
}

jobject RenderAPI_Android::createWindowSurface()
{
    DEBUG("Entering createWindowSurface");

    jclass activityThread = jni_env->FindClass("android/app/ActivityThread");
    jmethodID currentApplication = jni_env->GetStaticMethodID(activityThread, "currentApplication", "()Landroid/app/Application;");
    jobject app = jni_env->CallStaticObjectMethod(activityThread, currentApplication);

    jclass contextClass = jni_env->FindClass("android/content/Context");
    jmethodID getClassLoader = jni_env->GetMethodID(contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject classLoader = jni_env->CallObjectMethod(app, getClassLoader);

    jclass classLoaderClass = jni_env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = jni_env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

    jstring className = jni_env->NewStringUTF("org.videolan.libvlc.AWindow");
    jclass cls_JavaClass = (jclass)jni_env->CallObjectMethod(classLoader, loadClass, className);
    jni_env->DeleteLocalRef(className);

    if(cls_JavaClass == nullptr)
    {
        DEBUG("Failed to find class org.videolan.libvlc.AWindow");
        abort();
    }
    // find constructor method
    jmethodID mid_JavaClass = jni_env->GetMethodID (cls_JavaClass, "<init>", "(Lorg/videolan/libvlc/AWindow$SurfaceCallback;)V");

    // create object instance
    jobject obj_JavaClass = jni_env->NewObject(cls_JavaClass, mid_JavaClass, nullptr);
    // return object with a global reference
    return jni_env->NewGlobalRef(obj_JavaClass);
}

void RenderAPI_Android::destroyWindowSurface(jobject obj)
{
    if (obj != nullptr)
        jni_env->DeleteGlobalRef(obj);
}

void RenderAPI_Android::setVlcContext(libvlc_media_player_t *mp)
{
    if(RenderAPI_OpenEGL::unity_context == EGL_NO_CONTEXT) {
        DEBUG("[Android] No OpenGL context retrieved");
        return;
    }

    DEBUG("[Android] setVlcContext %p", this);
    if (m_awindow == nullptr)
        m_awindow = createWindowSurface();

    if (m_awindow != nullptr)
        libvlc_media_player_set_android_context(mp, m_awindow);
    else
        DEBUG("[Android] can't create window surface for media codec");

    RenderAPI_OpenEGL::setVlcContext(mp);
}
