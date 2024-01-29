#ifndef RENDER_API_OPENGL_CGL_H
#define RENDER_API_OPENGL_CGL_H

#include "RenderAPI.h"
#include "PlatformBase.h"
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <CoreVideo/CVMetalTextureCache.h>
#import <CoreVideo/CoreVideo.h>
#include <mutex>
#if defined(SHOW_WATERMARK)
#include "RenderAPI_OpenGLWatermark.h"
#endif

typedef struct libvlc_media_player_t libvlc_media_player_t;

struct RenderAPICoreVideoBuffer
{
    CVPixelBufferRef cvpx = nil;
    CVOpenGLESTextureRef texture = nil;
    CVMetalTextureRef texture_metal = nil;
    GLuint fbo = 0;
    GLuint texture_name = 0;

    ~RenderAPICoreVideoBuffer()
    {
        if (texture)
            CFRelease(texture);
        if (cvpx)
            CFRelease(cvpx);
        if (texture_metal)
            CFRelease(texture_metal);
    }

    RenderAPICoreVideoBuffer& operator=(RenderAPICoreVideoBuffer &&other)
    {
        cvpx = other.cvpx;
        other.cvpx = nil;
        texture = other.texture;
        other.texture = nil;
        texture_metal = other.texture_metal;
        other.texture_metal = nil;
        fbo = other.fbo;
        texture_name = other.texture_name;
        return *this;
    }

    RenderAPICoreVideoBuffer(){}
    RenderAPICoreVideoBuffer(RenderAPICoreVideoBuffer &&other)
    {
        *this = std::forward<RenderAPICoreVideoBuffer>(other);
    }
};

class RenderAPI_OpenGLEAGL : public RenderAPI
{
public:
    RenderAPI_OpenGLEAGL(UnityGfxRenderer apiType);
    ~RenderAPI_OpenGLEAGL() override { }

    void setVlcContext(libvlc_media_player_t *mp) override;
    void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;

    bool makeCurrent(bool current);
    static void* get_proc_address(void* /*data*/, const char* current);
    void swap(void* opaque);

    static bool setup(void **opaque, const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out);
    static void cleanup(void* opaque);
    static bool resize(void* opaque, const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *output);


    void* getVideoFrame(unsigned width, unsigned height, bool* out_updated) override;
private:
    void releaseFrameBufferResources();
    RenderAPICoreVideoBuffer initBuffer(unsigned width, unsigned height);

    CVOpenGLESTextureCacheRef _textureCache;
    CVMetalTextureCacheRef _textureCacheMetal;
    EAGLContext* m_context;

    RenderAPICoreVideoBuffer buffers[3];

    std::mutex text_lock;
    unsigned width;
    unsigned height;
    GLuint fbo[3];
    size_t idx_render = 0;
    size_t idx_swap = 1;
    size_t idx_display = 2;
    bool updated;

#if defined(SHOW_WATERMARK)
    OpenGLWatermark watermark;
#endif
};

#endif /* RENDER_API_OPENGL_CGL_H */
