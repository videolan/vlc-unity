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

    virtual void setVlcContext(libvlc_media_player_t *mp, void* textureHandle) override;
	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;

    static bool  make_current(void* data, bool current);
    static void* get_proc_address(void* /*data*/, const char* current);

private:
	UnityGfxRenderer m_APIType;

    EGLDisplay m_display;
    EGLSurface m_surface;
    EGLContext m_context;
};

#endif /* RENDER_API_OPENGL_EGL_H */
