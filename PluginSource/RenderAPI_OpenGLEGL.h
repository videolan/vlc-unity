#ifndef RENDER_API_OPENGL_EGL_H
#define RENDER_API_OPENGL_EGL_H

#include "RenderAPI_OpenGLBase.h"
#include "PlatformBase.h"
#include <EGL/egl.h>

class RenderAPI_OpenEGL : public RenderAPI_OpenGLBase
{
public:
	RenderAPI_OpenEGL(UnityGfxRenderer apiType);
	virtual ~RenderAPI_OpenEGL() { }

    virtual void setVlcContext(libvlc_media_player_t *mp) override;
	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
    virtual void retrieveOpenGLContext() override;
    virtual void ensureCurrentContext() override;
    virtual bool makeCurrent(bool current) override;

    static void* get_proc_address(void* /*data*/, const char* current);

protected:
    EGLDisplay m_display;
    EGLSurface m_surface;
    EGLContext m_context;
    static EGLContext unity_context;
};

#endif /* RENDER_API_OPENGL_EGL_H */
