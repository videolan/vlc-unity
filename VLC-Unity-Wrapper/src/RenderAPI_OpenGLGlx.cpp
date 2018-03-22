#include "RenderAPI_OpenGLBase.h"
#include "PlatformBase.h"
#include "Log.h"

#include <assert.h>

# define GL_GLEXT_PROTOTYPES
# include "GLEW/glew.h"
# include <X11/Xlib.h>
# include <GL/glx.h>

class RenderAPI_OpenGLX : public RenderAPI_OpenGLBase
{
public:
    RenderAPI_OpenGLX(UnityGfxRenderer apiType);
	virtual ~RenderAPI_OpenGLX() { }

    virtual void setVlcContext(libvlc_media_player_t *mp, void* textureHandle) override;

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;

    static bool  make_current(void* data, bool current);
    static void* get_proc_address(void* /*data*/, const char* current);

    Display * m_dpy;
    GLXWindow m_win;
    GLXContext m_glContext;
};

RenderAPI* CreateRenderAPI_OpenGLX(UnityGfxRenderer apiType)
{
	return new RenderAPI_OpenGLX(apiType);
}

RenderAPI_OpenGLX::RenderAPI_OpenGLX(UnityGfxRenderer apiType) :
    RenderAPI_OpenGLBase(apiType)
{
}

bool RenderAPI_OpenGLX::make_current(void* data, bool current)
{
    RenderAPI_OpenGLX* that = reinterpret_cast<RenderAPI_OpenGLX*>(data);
    if (current)
        return glXMakeContextCurrent (that->m_dpy, that->m_win, that->m_win, that->m_glContext);
    else
        return glXMakeContextCurrent (that->m_dpy, None, None, NULL);
}

void* RenderAPI_OpenGLX::get_proc_address(void* /*data*/, const char* procname)
{
    void* p =  (void*)glXGetProcAddressARB ((const GLubyte *)procname);
    DEBUG("get_proc_address %s %p", procname, p);
    return p;
}

void RenderAPI_OpenGLX::setVlcContext(libvlc_media_player_t *mp, void* textureHandle)
{
    DEBUG("setVlcContext %p", this);
    libvlc_video_set_opengl_callbacks(mp, create_fbo, destroy_fbo, make_current, get_proc_address, render_fbo, this);
}


void RenderAPI_OpenGLX::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	if (type == kUnityGfxDeviceEventInitialize) {

        DEBUG("kUnityGfxDeviceEventInitialize");

        /* Initialize GLX display */
        m_dpy = glXGetCurrentDisplay();

        GLXContext context = glXGetCurrentContext();
        if (m_dpy == NULL)
            return;

        //if (!CheckGLX (obj, dpy))
        //    goto error;

        const int snum = XDefaultScreen(m_dpy);
        const VisualID visual = XDefaultVisual(m_dpy, snum)->visualid;

        static const int attr[] = {
            GLX_RED_SIZE, 5,
            GLX_GREEN_SIZE, 5,
            GLX_BLUE_SIZE, 5,
            GLX_DOUBLEBUFFER, True,
            GLX_X_RENDERABLE, True,
            GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            None
        };

        int nelem;
        GLXFBConfig *confs = glXChooseFBConfig (m_dpy, snum, attr, &nelem);
        if (confs == NULL) {
            DEBUG("cannot choose GLX frame buffer configurations");
            return;
        }

        GLXFBConfig conf;
        bool found = false;
        for (int i = 0; i < nelem && !found; i++) {
            conf = confs[i];

            XVisualInfo *vi = glXGetVisualFromFBConfig (m_dpy, conf);
            if (vi == NULL)
                continue;

            if (vi->visualid == visual)
                found = true;
            XFree (vi);
        }
        XFree (confs);

        if (!found) {
            DEBUG("cannot match GLX frame buffer configuration");
            XCloseDisplay (m_dpy);
            return;
        }

        /* Create a drawing surface */
        int pBufferAttribs[] = {
            GLX_PBUFFER_WIDTH, (int)1024,
            GLX_PBUFFER_HEIGHT, (int)1024,
            None
        };
        m_win = glXCreatePbuffer(m_dpy, conf, pBufferAttribs);
        if (m_win == None) {
            DEBUG("cannot create GLX window");
            return;
        }

        /* Create a shared OpenGL context */
        m_glContext = glXCreateNewContext (m_dpy, conf, GLX_RGBA_TYPE, context, True);
        if (m_glContext == NULL) {
            glXDestroyWindow (m_dpy, m_win);
            DEBUG("cannot create GLX context");
            return;
        }
    }
}
