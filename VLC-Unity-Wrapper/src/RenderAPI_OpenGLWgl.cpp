#include "RenderAPI_OpenGLBase.h"
#include "PlatformBase.h"
#include "Log.h"

#include "windows.h"
#include "wingdi.h"

typedef HGLRC(WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShateContext, const int *attribList);
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
typedef BOOL (WINAPI  * PFNWGLSWAPINTERVALEXTPROC)(int interval);

class RenderAPI_OpenWGL : public RenderAPI_OpenGLBase
{
public:
	RenderAPI_OpenWGL(UnityGfxRenderer apiType);
	virtual ~RenderAPI_OpenWGL() { }

    virtual void setVlcContext(libvlc_media_player_t *mp) override;
	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;

    static bool  make_current(void* data, bool current);
    static void* get_proc_address(void* /*data*/, const char* current);

private:
	UnityGfxRenderer m_APIType;

    HDC m_hdc;
    HGLRC m_glContext;

};

RenderAPI* CreateRenderAPI_OpenWGL(UnityGfxRenderer apiType)
{
	return new RenderAPI_OpenWGL(apiType);
}


RenderAPI_OpenWGL::RenderAPI_OpenWGL(UnityGfxRenderer apiType) :
    RenderAPI_OpenGLBase(apiType)
{
}


bool RenderAPI_OpenWGL::make_current(void* data, bool current)
{
    RenderAPI_OpenWGL* that = reinterpret_cast<RenderAPI_OpenWGL*>(data);
    if (current)
        return wglMakeCurrent(that->m_hdc, that->m_glContext);
    else
        return wglMakeCurrent(that->m_hdc, NULL);
}

void* RenderAPI_OpenWGL::get_proc_address(void* /*data*/, const char* procname)
{
    void* p =  (void*)wglGetProcAddress (procname);
    DEBUG("get_proc_address %s %p", procname, p);
    return p;
}

void RenderAPI_OpenWGL::setVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("setVlcContext %p", this);
    libvlc_video_set_opengl_callbacks(mp, create_fbo, destroy_fbo, make_current, get_proc_address, render_fbo, this);
}

void RenderAPI_OpenWGL::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	if (type == kUnityGfxDeviceEventInitialize) {
        DEBUG("kUnityGfxDeviceEventInitialize");

        m_hdc = wglGetCurrentDC();
        HGLRC glContext = wglGetCurrentContext();

        DEBUG("wglGetCurrentDC %p wglGetCurrentContext %p", m_hdc, glContext);

        wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(wglGetProcAddress("wglCreateContextAttribsARB"));
        if ( wglCreateContextAttribsARB ) {
            DEBUG("USING wglCreateContextAttribsARB");
            static const int contextAttribs [] = {
                0
            };
            m_glContext = wglCreateContextAttribsARB(m_hdc, glContext, contextAttribs);
        } else {
            DEBUG("NO wglCreateContextAttribsARB");
            m_glContext = wglCreateContext(m_hdc);
            if (! m_glContext ) {
                DEBUG("!!! no m_glContext %lu", GetLastError());
                return;
            }

            if (!wglShareLists(glContext, m_glContext)) {
                DEBUG("!!! no wglShareLists %lu", GetLastError());
                return;
            }
        }

        /**
        wglMakeCurrent(m_hdc, m_glContext);
        PFNWGLSWAPINTERVALEXTPROC SwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        if (SwapIntervalEXT)
            SwapIntervalEXT(1);
        wglMakeCurrent(m_hdc, NULL);
        */

	} else if (type == kUnityGfxDeviceEventShutdown) {
        wglDeleteContext(m_glContext);
	}
}
