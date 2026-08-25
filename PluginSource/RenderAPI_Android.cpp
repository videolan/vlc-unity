#include <dlfcn.h>
#include <jni.h>
#include "AndroidJNI.h"
#include "Log.h"
#include "RenderAPI_OpenGLEGL.h"

static JavaVM* java_vm = nullptr;
static void* handle = nullptr;
typedef jint (*JNI_OnLoad_pf)(JavaVM *, void*);
typedef void (*JNI_OnUnload_pf)(JavaVM *, void*);

namespace {

class ScopedJNIEnv
{
public:
    ScopedJNIEnv()
    {
        m_vm = java_vm;
        if (!m_vm)
            return;
        const jint result = m_vm->GetEnv(
            reinterpret_cast<void**>(&m_env), JNI_VERSION_1_6);
        if (result == JNI_EDETACHED &&
            m_vm->AttachCurrentThread(&m_env, nullptr) == JNI_OK) {
            m_attached = true;
        } else if (result != JNI_OK) {
            m_env = nullptr;
        }
    }

    ~ScopedJNIEnv()
    {
        if (m_attached)
            m_vm->DetachCurrentThread();
    }

    JNIEnv* get() const { return m_env; }

private:
    JavaVM* m_vm = nullptr;
    JNIEnv* m_env = nullptr;
    bool m_attached = false;
};

} // namespace

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void)reserved;
    DEBUG("ENTERED RENDERAPI_ANDROID.CPP -> JNI_ONLOAD");

    JNIEnv* env = nullptr;
    if (vm->GetEnv(
            reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    java_vm = vm;

    handle = dlopen("libvlc.so", RTLD_LAZY);
    if (!handle)
    {
        DEBUG("could not link libvlc.so");
        java_vm = nullptr;
        return JNI_ERR;
    }

    DEBUG("(JNI_OnLoad_pf) dlsym(handle, JNI_OnLoad);");

    JNI_OnLoad_pf load = reinterpret_cast<JNI_OnLoad_pf>(
        dlsym(handle, "JNI_OnLoad"));
    if (!load || load(vm, env) < 0)
    {
        if (!load)
            DEBUG("could not find VLC JNI_OnLoad");
        else
            DEBUG("VLC JNI_OnLoad failed");
        dlclose(handle);
        handle = nullptr;
        java_vm = nullptr;
        return JNI_ERR;
    }


    DEBUG("[Android] initialized JNI with VM %p", java_vm);
    DEBUG("Exiting...");

    return JNI_VERSION_1_6;
}


void JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)reserved;
    DEBUG("[Android] unload JNI VM %p", java_vm);

    JNIEnv* env = nullptr;
    (void)vm->GetEnv(
        reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (handle) {
        JNI_OnUnload_pf unload = reinterpret_cast<JNI_OnUnload_pf>(
            dlsym(handle, "JNI_OnUnload"));
        if (unload)
            unload(vm, env);
        else
            DEBUG("could not find VLC JNI_OnUnload");
        dlclose(handle);
        handle = nullptr;
    }

    java_vm = nullptr;
}

jobject AndroidCreateAWindow(const char* logTag)
{
    ScopedJNIEnv scopedEnv;
    JNIEnv* env = scopedEnv.get();
    if (!env) {
        DEBUG("%s no JNIEnv is available", logTag);
        return nullptr;
    }
    if (env->PushLocalFrame(16) != JNI_OK) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        DEBUG("%s failed to create a JNI local frame", logTag);
        return nullptr;
    }

    jobject globalWindow = nullptr;
    do {
        jclass activityThread = env->FindClass("android/app/ActivityThread");
        if (!activityThread)
            break;
        jmethodID currentApplication = env->GetStaticMethodID(
            activityThread, "currentApplication", "()Landroid/app/Application;");
        if (!currentApplication)
            break;
        jobject app = env->CallStaticObjectMethod(
            activityThread, currentApplication);
        if (!app)
            break;

        jclass contextClass = env->FindClass("android/content/Context");
        if (!contextClass)
            break;
        jmethodID getClassLoader = env->GetMethodID(
            contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        if (!getClassLoader)
            break;
        jobject classLoader = env->CallObjectMethod(app, getClassLoader);
        if (!classLoader)
            break;

        jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
        if (!classLoaderClass)
            break;
        jmethodID loadClass = env->GetMethodID(
            classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if (!loadClass)
            break;
        jstring className = env->NewStringUTF("org.videolan.libvlc.AWindow");
        if (!className)
            break;
        jclass windowClass = static_cast<jclass>(
            env->CallObjectMethod(classLoader, loadClass, className));
        if (!windowClass)
            break;

        jmethodID constructor = env->GetMethodID(
            windowClass, "<init>",
            "(Lorg/videolan/libvlc/AWindow$SurfaceCallback;)V");
        if (!constructor)
            break;
        jobject window = env->NewObject(windowClass, constructor, nullptr);
        if (window)
            globalWindow = env->NewGlobalRef(window);
    } while (false);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (globalWindow)
            env->DeleteGlobalRef(globalWindow);
        globalWindow = nullptr;
    }
    env->PopLocalFrame(nullptr);
    if (!globalWindow)
        DEBUG("%s failed to create org.videolan.libvlc.AWindow", logTag);
    return globalWindow;
}

void AndroidDeleteGlobalRef(jobject object)
{
    if (!object)
        return;
    ScopedJNIEnv scopedEnv;
    JNIEnv* env = scopedEnv.get();
    if (env)
        env->DeleteGlobalRef(object);
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
    return AndroidCreateAWindow("[Android]");
}

void RenderAPI_Android::destroyWindowSurface(jobject obj)
{
    AndroidDeleteGlobalRef(obj);
}

void RenderAPI_Android::setVlcContext(libvlc_media_player_t *mp)
{
    if(RenderAPI_OpenEGL::unity_context == EGL_NO_CONTEXT) {
        DEBUG("[Android] No OpenGL context retrieved yet");
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
