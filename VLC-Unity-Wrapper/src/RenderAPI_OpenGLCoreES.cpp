#include "RenderAPI.h"
#include "PlatformBase.h"

// OpenGL Core profile (desktop) or OpenGL ES (mobile) implementation of RenderAPI.
// Supports several flavors: Core, ES2, ES3


#if SUPPORT_OPENGL_UNIFIED


#include <assert.h>
#if UNITY_IPHONE
#	include <OpenGLES/ES2/gl.h>
#elif UNITY_ANDROID || UNITY_WEBGL
#	include <GLES2/gl2.h>
#else
#	include "GLEW/glew.h"
#endif

#if UNITY_LINUX
#    include <X11/Xlib.h>
#    include <GL/glx.h>
#elif UNITY_WIN
#    include  "windows.h"
#    include "wingdi.h"
#endif

class RenderAPI_OpenGLCoreES : public RenderAPI
{
public:
	RenderAPI_OpenGLCoreES(UnityGfxRenderer apiType);
	virtual ~RenderAPI_OpenGLCoreES() { }

    virtual void setVlcContext(libvlc_media_player_t *mp, void* textureHandle) override;

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
	virtual void* BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch) override;
	virtual void EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr) override;

private:
	UnityGfxRenderer m_APIType;

#if UNITY_LINUX
    Display * m_dpy;
    GLXContext m_glContext;
#elif UNITY_WIN
    HDC m_hdc;
    HGLRC m_glContext;

#endif
};


RenderAPI* CreateRenderAPI_OpenGLCoreES(UnityGfxRenderer apiType)
{
	return new RenderAPI_OpenGLCoreES(apiType);
}


RenderAPI_OpenGLCoreES::RenderAPI_OpenGLCoreES(UnityGfxRenderer apiType)
	: m_APIType(apiType)
{
}


void RenderAPI_OpenGLCoreES::setVlcContext(libvlc_media_player_t *mp, void* textureHandle)
{
#if UNITY_LINUX
    libvlc_video_set_glx_opengl_context(mp, m_dpy, m_glContext, (GLuint)(size_t)textureHandle);
#elif UNITY_WIN
    libvlc_video_set_wgl_opengl_context(mp, m_hdc, m_glContext, (GLuint)(size_t)textureHandle);
#endif
}


void RenderAPI_OpenGLCoreES::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	if (type == kUnityGfxDeviceEventInitialize)
	{
#if UNITY_LINUX
        m_dpy = glXGetCurrentDisplay();
        m_glContext = glXGetCurrentContext();
#elif UNITY_WIN
        m_hdc = wglGetCurrentDC();
        m_glContext = wglGetCurrentContext();
#endif
	}
	else if (type == kUnityGfxDeviceEventShutdown)
	{
		//@TODO: release resources
	}
}


void* RenderAPI_OpenGLCoreES::BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch)
{
    //N/A
    return nullptr;
}


void RenderAPI_OpenGLCoreES::EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr)
{
    return;
}

#endif // #if SUPPORT_OPENGL_UNIFIED
