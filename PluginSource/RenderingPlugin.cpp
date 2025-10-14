#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Log.h"

#include <map>

#if defined(SUPPORT_D3D11)
#include <windows.h>
#endif

#if defined(UNITY_ANDROID)
#include "RenderAPI_Vulkan.h"
#endif

extern "C" {
#include <stdlib.h>
#include <unistd.h>
#include <vlc/vlc.h>
#include <string.h>
}

static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

libvlc_instance_t * inst;

static IUnityGraphics* s_Graphics = NULL;
static std::map<libvlc_media_player_t*,RenderAPI*> contexts = {};
static IUnityInterfaces* s_UnityInterfaces = NULL;

static int s_color_space;

/** LibVLC's API function exported to Unity
 *
 * Every following functions will be exported to. Unity We have to
 * redeclare the LibVLC's function for the keyword
 * UNITY_INTERFACE_EXPORT and UNITY_INTERFACE_API
 */

#if defined(__APPLE__)
# import <TargetConditionals.h>
# include <cstdlib>
#endif

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetPluginPath(char* path)
{
#if defined(SUPPORT_D3D11) && !defined(UWP)
    DEBUG("SetPluginPath \n");
    DEBUG("_putenv_s with VLC_PLUGIN_PATH -> %s \n", path);
    auto e = _putenv_s("VLC_PLUGIN_PATH", path);
    if(e != 0)
        DEBUG("_putenv_s failed \n");
    else DEBUG("_putenv_s succeeded \n");
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
    DEBUG("SetPluginPath \n");
    DEBUG("setenv with VLC_PLUGIN_PATH -> %s \n", path);
    auto e = setenv("VLC_PLUGIN_PATH", path, 1);
    if(e != 0)
        DEBUG("setenv failed \n");
    else DEBUG("setenv succeeded \n");

#endif
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_set_color_space(int color_space)
{
    s_color_space = color_space;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_set_bit_depth_format(libvlc_media_player_t* mp, int bit_depth)
{
#if !defined(SUPPORT_D3D11)
    return;
#endif
    if(mp == NULL)
        return;

    if(bit_depth != 8 /* && bit_depth != 10 */ && bit_depth != 16)
        return;

    RenderAPI* s_CurrentAPI = contexts.find(mp)->second;
    if(!s_CurrentAPI)
    {
        return;
    }

    s_CurrentAPI->setbitDepthFormat(bit_depth);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Print(char* toPrint)
{
    DEBUG("%s", toPrint);
}

extern "C" libvlc_media_player_t* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_media_player_new(libvlc_instance_t* libvlc)
{
    if(libvlc == NULL)
    {
        DEBUG("libvlc is NULL, aborting...");
        return NULL;
    }

    inst = libvlc;

    DEBUG("LAUNCH");

    if (inst == NULL) {
        DEBUG("LibVLC is not instanciated");
        return NULL;
    }

    libvlc_media_player_t * mp;

    mp = libvlc_media_player_new(inst);

    RenderAPI* s_CurrentAPI;

    if (mp == NULL) {
        DEBUG("Error initializing media player");
        goto err;
    }

    DEBUG("Calling... Initialize Render API \n");

    s_DeviceType = s_Graphics->GetRenderer();
    if(s_DeviceType == kUnityGfxRendererNull)
    {
        DEBUG("s_DeviceType is NULL \n");    
        return NULL; 
    }
    
    DEBUG("Calling... CreateRenderAPI \n");
    DEBUG("s_DeviceType = %d \n", s_DeviceType);

    s_CurrentAPI = CreateRenderAPI(s_DeviceType);

    if(s_CurrentAPI == NULL)
    {
        DEBUG("s_CurrentAPI is NULL \n");
        return NULL;
    }    
    
    DEBUG("Calling... ProcessDeviceEvent \n");
    
    s_CurrentAPI->ProcessDeviceEvent(kUnityGfxDeviceEventInitialize, s_UnityInterfaces);
    s_CurrentAPI->setColorSpace(s_color_space);

    DEBUG("Calling... setVlcContext s_CurrentAPI=%p mp=%p", s_CurrentAPI, mp);
    s_CurrentAPI->setVlcContext(mp);

    contexts[mp] = s_CurrentAPI;

    return mp;
err:
    if ( mp ) {
        // Stop playing
        libvlc_media_player_stop_async (mp);

        // Free the media_player
        libvlc_media_player_release (mp);
        mp = NULL;
    }    
    return NULL;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_media_player_release(libvlc_media_player_t* mp)
{
    if(mp == NULL)
        return;

    RenderAPI* s_CurrentAPI = contexts.find(mp)->second;
    
    if(s_CurrentAPI == NULL)
        return;

    s_CurrentAPI->unsetVlcContext(mp);

    contexts.erase(mp);
    
    libvlc_media_player_release(mp);
}

extern "C" void* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_get_texture(libvlc_media_player_t* mp, unsigned width, unsigned height, bool * updated)
{
    *updated = false;

    if(mp == NULL || !libvlc_media_player_is_playing(mp))
        return NULL;

    if(width == 0 && height == 0)
        return NULL;

    RenderAPI* s_CurrentAPI = contexts.find(mp)->second;

    if (!s_CurrentAPI) {
        DEBUG("Error, no Render API");
        if (updated)
            *updated = false;
        return nullptr;
    }

    return s_CurrentAPI->getVideoFrame(width, height, updated);
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_set_unity_texture_vulkan(libvlc_media_player_t* mp, void* unityTexturePtr)
{
    if(mp == NULL) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: mp is NULL");
        return false;
    }

    auto it = contexts.find(mp);
    if(it == contexts.end()) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: no context found for mp");
        return false;
    }

    RenderAPI* s_CurrentAPI = it->second;
    if (!s_CurrentAPI) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: s_CurrentAPI is NULL");
        return false;
    }

#if defined(UNITY_ANDROID)
    // Check if this is the Vulkan renderer
    if (s_DeviceType == kUnityGfxRendererVulkan) {
        // Cast to RenderAPI_Vulkan and call setUnityTexture
        RenderAPI_Vulkan* vulkanAPI = static_cast<RenderAPI_Vulkan*>(s_CurrentAPI);
        return vulkanAPI->setUnityTexture(unityTexturePtr);
    }
#endif

    DEBUG("libvlc_unity_set_unity_texture_vulkan: not on Vulkan renderer");
    return false;
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API VLCUnity_UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    DEBUG("UnityPluginLoad");
    s_UnityInterfaces = unityInterfaces;
    s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
    s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

#if defined(UNITY_ANDROID)
    // Initialize Vulkan validation layers BEFORE any Vulkan instance creation
    // This must be called before kUnityGfxDeviceEventInitialize
    extern void InitializeVulkanValidation(IUnityInterfaces* interfaces);
    InitializeVulkanValidation(unityInterfaces);
#endif

    // Run OnGraphicsDeviceEvent(initialize) manually on plugin load
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    VLCUnity_UnityPluginLoad(unityInterfaces);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API VLCUnity_UnityPluginUnload()
{
  s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
    VLCUnity_UnityPluginUnload();
}

static RenderAPI* EarlyRenderAPI = NULL;

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
    // Create graphics API implementation upon initialization
       if (eventType == kUnityGfxDeviceEventInitialize) {
            DEBUG("Initialise Render API");
            if (EarlyRenderAPI != NULL) {
                DEBUG("*** EarlyRenderAPI != NULL while initialising ***");
                return;
            }

        DEBUG("s_Graphics->GetRenderer() \n");

        s_DeviceType = s_Graphics->GetRenderer();

        DEBUG("CreateRenderAPI(s_DeviceType) \n");
        DEBUG("s_DeviceType = %d \n", s_DeviceType);

        EarlyRenderAPI = CreateRenderAPI(s_DeviceType);
        return;
    }

    if(EarlyRenderAPI){
        EarlyRenderAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
    } else {
        DEBUG("Unable to process event, no Render API");
    }

    // Let the implementation process the device related events
    std::map<libvlc_media_player_t*, RenderAPI*>::iterator it;

    for(it = contexts.begin(); it != contexts.end(); it++)
    {
        RenderAPI* currentAPI = it->second;
        if(currentAPI) {
            DEBUG(" currentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces); \n");
            currentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
        }
    }
}

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
    DEBUG("OnRenderEvent called \n");
#if defined(UNITY_ANDROID)
    if(EarlyRenderAPI)
    {
        EarlyRenderAPI->retrieveOpenGLContext();
    }
#endif
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
    return OnRenderEvent;
}
