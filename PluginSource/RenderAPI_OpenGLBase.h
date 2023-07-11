#ifndef RENDERAPI_OPENGLBASE_H
#define RENDERAPI_OPENGLBASE_H

#include "RenderAPI.h"
#include "PlatformBase.h"

#if defined(UNITY_IPHONE)
#	import <OpenGLES/ES2/gl.h>
#elif defined(UNITY_OSX)
#  import <OpenGL/GL.h>
#elif defined(UNITY_ANDROID) || defined(UNITY_WEBGL)
#	include <GLES2/gl2.h>
#elif defined(UNITY_WIN)
#   define GL_GLEXT_PROTOTYPES
#	include "GL/glew.h"
#endif

//#if defined(UNITY_WIN)
//#  include <mingw.mutex.h>
//#else
#  include <mutex>
//#endif

#ifdef SHOW_WATERMARK
#  include "RenderAPI_OpenGLWatermark.h"
#endif

class RenderAPI_OpenGLBase : public RenderAPI
{
public:
	RenderAPI_OpenGLBase(UnityGfxRenderer apiType);
	virtual ~RenderAPI_OpenGLBase() { }

    virtual void setVlcContext(libvlc_media_player_t *mp) override = 0 ;
    virtual void retrieveOpenGLContext() override = 0 ;

    static bool setup(void **opaque, const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out);
    static void cleanup(void* opaque);
    static bool resize(void* opaque, const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *output);
    static void swap(void* opaque);

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override = 0;

    virtual bool makeCurrent(bool) = 0;

    void* getVideoFrame(unsigned width, unsigned height, bool* out_updated) override;

private:
    void releaseFrameBufferResources();

    std::mutex text_lock;
    unsigned width = 0;
    unsigned height = 0;
    GLuint tex[3];
    GLuint fbo[3];
    size_t idx_render = 0;
    size_t idx_swap = 1;
    size_t idx_display = 2;
    bool updated = false;

#ifdef SHOW_WATERMARK
    OpenGLWatermark watermark;
#endif
};


#endif /* RENDERAPI_OPENGLBASE_H */
