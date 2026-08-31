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
    virtual void performRenderThreadWork() {}
    // Queue submissions that require UnityVulkanGraphicsQueueAccess_Allow are
    // dispatched from a separately configured plugin event. Backends that do
    // not own Vulkan queue submissions leave this as a no-op.
    virtual void performQueueSubmissionWork() {}
    virtual bool setUnityTexture(void* unityTexturePtr) {
        (void)unityTexturePtr;
        return false;
    }
    virtual void beginShutdown() {}
    // Plugin unload cannot wait for a render-thread callback. Backends with
    // deferred GPU ownership must detach or delegate that ownership here so
    // their destructor is safe and deterministic.
    virtual void prepareForPluginUnload() { beginShutdown(); }
    virtual bool canDestroy() const { return true; }
    virtual bool isInitialized() const { return true; }
    virtual void setColorSpace(int color_space) {
        (void)color_space;
    }
    virtual void setbitDepthFormat(int bit_depth) {
        (void)bit_depth;
    }
};


// Create a graphics API implementation instance for the given API type.
RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType);
