#pragma once

#include "Unity/IUnityGraphics.h"
extern "C"
{
#include <vlc/vlc.h>
#include <stddef.h>
}

struct IUnityInterfaces;

// There are implementations of this base class for D3D9, D3D11, OpenGL etc.; see individual RenderAPI_* files.
class RenderAPI
{
public:
    virtual ~RenderAPI() { }

    // Process general event like initialization, shutdown, device loss/reset etc.
    virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) = 0;

    virtual void setVlcContext(libvlc_media_player_t *mp) {
        (void)mp;
    }
    virtual void unsetVlcContext(libvlc_media_player_t *mp) {
        (void)mp;
    }

    virtual void* getVideoFrame(unsigned height, unsigned width, bool* out_updated) {
        (void)height; (void)width;
        if (out_updated)
            *out_updated = false;
        return nullptr;
    }

    virtual void retrieveOpenGLContext() {}
    virtual void setColorSpace(int color_space) {
        (void)color_space;
    }
};


// Create a graphics API implementation instance for the given API type.
RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType);
