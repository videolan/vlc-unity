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

    virtual void setVlcContext(libvlc_media_player_t *mp) {}
    virtual void unsetVlcContext(libvlc_media_player_t *mp) {}

    virtual void* getVideoFrame(bool* out_updated) {
        if (out_updated)
            *out_updated = false;
        return nullptr;
    }
};


// Create a graphics API implementation instance for the given API type.
RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType);
