#ifndef RENDER_API_OPENGL_EGL_H
#define RENDER_API_OPENGL_EGL_H

#include "RenderAPI_OpenGLBase.h"
#include "PlatformBase.h"
#include <EGL/egl.h>

class RenderAPI_OpenEGL : public RenderAPI_OpenGLBase
{
public:
	RenderAPI_OpenEGL(UnityGfxRenderer apiType);
	~RenderAPI_OpenEGL() override;

    void setVlcContext(libvlc_media_player_t* mp) override;
    void unsetVlcContext(libvlc_media_player_t* mp) override;
	void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
    void retrieveOpenGLContext() override;
    void ensureCurrentContext() override;
    bool makeCurrent(bool current) override;
    bool isInitialized() const override
    {
        return m_display != EGL_NO_DISPLAY &&
               m_surface != EGL_NO_SURFACE &&
               m_context != EGL_NO_CONTEXT;
    }

    static void* get_proc_address(void* /*data*/, const char* current);

protected:
    void registerOutputCallbacks();
    void unregisterOutputCallbacks();
    void releaseResources();

    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLSurface m_surface = EGL_NO_SURFACE;
    EGLContext m_context = EGL_NO_CONTEXT;
    bool m_callbacksRegistered = false;
    static EGLContext unity_context;
};

#endif /* RENDER_API_OPENGL_EGL_H */
