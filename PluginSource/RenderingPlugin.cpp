#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Log.h"

#if defined(SHOW_WATERMARK) && !defined(UNITY_ANDROID)
#  include "watermark.png.h"
#  include <fstream>
#endif

#include <map>

#if defined(SUPPORT_D3D11)
#include <windows.h>
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

#if defined(SHOW_WATERMARK) && !defined(UNITY_ANDROID)
#if !UWP
    std::ofstream outfile;
    outfile.open("logo.png", std::ofstream::binary);
    outfile.write((const char*)watermark_png, watermark_png_len);
    outfile.close();

    libvlc_video_set_logo_string(mp, libvlc_logo_file, "logo.png");
    libvlc_video_set_logo_int(mp, libvlc_logo_enable, 1);
    libvlc_video_set_logo_int(mp, libvlc_logo_position, 10);
#else
    libvlc_video_set_marquee_string(mp, libvlc_marquee_Text, "Videolabs");
    libvlc_video_set_marquee_int(mp, libvlc_marquee_Enable, 1);
    libvlc_video_set_marquee_int(mp, libvlc_marquee_Position, 9);
    libvlc_video_set_logo_int(mp, libvlc_marquee_Size, 32);
#endif
#endif

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

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API VLCUnity_UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    DEBUG("UnityPluginLoad");
    s_UnityInterfaces = unityInterfaces;
    s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
    s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

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
