#ifndef RENDERAPI_OPENGLBASE_H
#define RENDERAPI_OPENGLBASE_H

#include "RenderAPI.h"
#include "PlatformBase.h"

#if UNITY_IPHONE
#	include <OpenGLES/ES2/gl.h>
#elif UNITY_ANDROID || UNITY_WEBGL
#	include <GLES2/gl2.h>
#else
#   define GL_GLEXT_PROTOTYPES
#	include "GLEW/glew.h"
#endif


#if UNITY_WIN
#  include <mingw.mutex.h>
#else
#  include <mutex>
#endif


class RenderAPI_OpenGLBase : public RenderAPI
{
public:
	RenderAPI_OpenGLBase(UnityGfxRenderer apiType);
	virtual ~RenderAPI_OpenGLBase() { }

    virtual void setVlcContext(libvlc_media_player_t *mp, void* textureHandle) = 0;

    static void create_fbo(void* data, size_t width, size_t height);
    static void destroy_fbo(void* data);
    static void render_fbo(void* data, bool lock);


	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) = 0;
	virtual void* BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch) override;
	virtual void EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr) override;

    void* getVideoFrame(bool* out_updated);

private:
	UnityGfxRenderer m_APIType;

    std::mutex text_lock;
    GLuint tex[3];
    GLuint fbo[3];
    size_t idx_render = 0;
    size_t idx_swap = 1;
    size_t idx_display = 2;
    bool updated = false;
};


#endif /* RENDERAPI_OPENGLBASE_H */
