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

	// Begin modifying texture data.
	// Returns pointer into the data buffer to write into (or NULL on failure), and pitch in bytes of a single texture row.
	virtual void* BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch) = 0;
	// End modifying texture data.
	virtual void EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr) = 0;

    virtual void setVlcContext(libvlc_media_player_t *mp, void*  textureHandle) {}

    virtual void* getVideoFrame(bool* out_updated) {
        if (out_updated)
            *out_updated = false;
        return nullptr;
    }
};


// Create a graphics API implementation instance for the given API type.
RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType);
