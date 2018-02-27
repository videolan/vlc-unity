#include "RenderAPI.h"
#include "PlatformBase.h"
#include "Log.h"
//#include <pthread.h>
#include <mingw.mutex.h>

// OpenGL Core profile (desktop) or OpenGL ES (mobile) implementation of RenderAPI.
// Supports several flavors: Core, ES2, ES3


#if SUPPORT_OPENGL_UNIFIED


#include <assert.h>
#if UNITY_IPHONE
#	include <OpenGLES/ES2/gl.h>
#elif UNITY_ANDROID || UNITY_WEBGL
#	include <GLES2/gl2.h>
#else
#   include <windows.h>
#define GL_GLEXT_PROTOTYPES
#	include "GLEW/glew.h"
#endif

#if UNITY_LINUX
#    include <X11/Xlib.h>
#    include <GL/glx.h>
#elif UNITY_WIN
#    include  "windows.h"
#    include "wingdi.h"

typedef HGLRC(WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShateContext, const int *attribList);
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;

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


    void* getVideoFrame(bool* out_updated);

    static void create_fbo(void* data, size_t width, size_t height);
    static void destroy_fbo(void* data);
    static void render_fbo(void* data, bool lock);

private:
	UnityGfxRenderer m_APIType;

#if UNITY_LINUX
    Display * m_dpy;
    GLXContext m_glContext;
#elif UNITY_WIN
    HDC m_hdc;
    HGLRC m_glContext;

#endif
    //pthread_mutex_t* text_lock = PTHREAD_MUTEX_INITIALIZER;;
    std::mutex text_lock;
    GLuint tex[3];
    GLuint fbo[3];
    size_t idx_render = 0;
    size_t idx_swap = 1;
    size_t idx_display = 2;
    bool updated = false;
};



RenderAPI* CreateRenderAPI_OpenGLCoreES(UnityGfxRenderer apiType)
{
    glewExperimental=TRUE;
    if (glewInit() != GLEW_OK) {
        DEBUG("unable to initialise GLEW");
    } else {
        DEBUG("GLEW init OK");
    }

    glGetError();
	return new RenderAPI_OpenGLCoreES(apiType);
}


RenderAPI_OpenGLCoreES::RenderAPI_OpenGLCoreES(UnityGfxRenderer apiType)
	: m_APIType(apiType)
{
}


void RenderAPI_OpenGLCoreES::create_fbo(void* data, size_t width, size_t height)
{
    DEBUG("create_fbo %p, %lu x %lu", data, width, height);
    RenderAPI_OpenGLCoreES* that = reinterpret_cast<RenderAPI_OpenGLCoreES*>(data);
    glGenTextures(3, that->tex);
    glGenFramebuffersEXT(3, that->fbo);

    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, that->tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        DEBUG("bind FBO");
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, that->fbo[i]);

        DEBUG("FB Texture 2D");
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, that->tex[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);

    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        DEBUG("failed to create the FBO");
        return;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

void RenderAPI_OpenGLCoreES::destroy_fbo(void* data)
{
    DEBUG("destroy_fbo");
    RenderAPI_OpenGLCoreES* that = reinterpret_cast<RenderAPI_OpenGLCoreES*>(data);
    glDeleteTextures(3, that->tex);
    glDeleteFramebuffersEXT(3, that->fbo);
}

void RenderAPI_OpenGLCoreES::render_fbo(void* data, bool enter)
{
    RenderAPI_OpenGLCoreES* that = reinterpret_cast<RenderAPI_OpenGLCoreES*>(data);
    if (enter) {
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, that->fbo[that->idx_render]);
    } else {
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
        {
            std::lock_guard<std::mutex> lock(that->text_lock);
            that->updated = true;
            std::swap(that->idx_swap, that->idx_render);
        }
    }
}

void RenderAPI_OpenGLCoreES::setVlcContext(libvlc_media_player_t *mp, void* textureHandle)
{
#if UNITY_LINUX
    libvlc_video_set_glx_opengl_context(mp, m_dpy, m_glContext, (GLuint)(size_t)textureHandle);
#elif UNITY_WIN
    DEBUG("setVlcContext %p", this);
    libvlc_video_set_wgl_opengl_context(mp, m_hdc, m_glContext,
        create_fbo, destroy_fbo, render_fbo, this);
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
        //HWND hwnd = GetForegroundWindow(); // Any window will do - we don't render to it
        //if(!hwnd) { OutputDebugString("InitOpenGL error 1"); return; }

        //m_hdc = GetDC(hwnd);
        m_hdc = wglGetCurrentDC();
        HGLRC glContext = wglGetCurrentContext();

        DEBUG("wglGetCurrentDC %p wglGetCurrentContext %p", m_hdc, glContext);


        wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(wglGetProcAddress("wglCreateContextAttribsARB"));
        if ( wglCreateContextAttribsARB )
        {
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

            if (!wglShareLists(glContext, m_glContext))
            {
                DEBUG("!!! no wglShareLists %lu", GetLastError());
                return;
            }

        }
#endif
	}
	else if (type == kUnityGfxDeviceEventShutdown)
	{
#if UNITY_WIN
        wglDeleteContext(m_glContext);
		//@TODO: release resources
#endif
	}
}

void* RenderAPI_OpenGLCoreES::getVideoFrame(bool* out_updated)
{
    std::lock_guard<std::mutex> lock(text_lock);
    if (out_updated)
        *out_updated = updated;
    if (updated)
    {
        std::swap(idx_swap, idx_display);
        updated = false;
    }
    return (void*)(size_t)tex[idx_display];
}

void* RenderAPI_OpenGLCoreES::BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch)
{
    //HDC hdc = wglGetCurrentDC();
    //HGLRC glrc = wglGetCurrentContext();
    //DEBUG("wglGetCurrentDC %p wglGetCurrentContext %p", hdc, glrc);

    return nullptr;
}


void RenderAPI_OpenGLCoreES::EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr)
{
    return;
}

#endif // #if SUPPORT_OPENGL_UNIFIED
