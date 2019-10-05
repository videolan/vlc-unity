#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Log.h"

#include <map>
#include <windows.h>

extern "C" {
#include <stdlib.h>
#include <unistd.h>
#include <vlc/vlc.h>
#include <string.h>
}

static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;
static int   g_TextureRowPitch = 0;

libvlc_instance_t * inst;
libvlc_media_player_t * mp;

static IUnityGraphics* s_Graphics = NULL;
static std::map<libvlc_media_player_t*,RenderAPI*> contexts = {};
static IUnityInterfaces* s_UnityInterfaces = NULL;

static UINT Width;
static UINT Height;
static void* Hwnd;

/** LibVLC's API function exported to Unity
 *
 * Every following functions will be exported to. Unity We have to
 * redeclare the LibVLC's function for the keyword
 * UNITY_INTERFACE_EXPORT and UNITY_INTERFACE_API
 */

extern "C" libvlc_media_player_t* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
CreateAndInitMediaPlayer(libvlc_instance_t* libvlc)
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

    mp = libvlc_media_player_new(inst);
    RenderAPI* s_CurrentAPI;

    if (mp == NULL) {
        DEBUG("Error initializing media player");
        goto err;
    }

    DEBUG("Calling... Initialize Render API \n");

    s_DeviceType = s_Graphics->GetRenderer();

    DEBUG("Calling... CreateRenderAPI \n");

    s_CurrentAPI = CreateRenderAPI(s_DeviceType);
    
    if(s_CurrentAPI == NULL)
    {
        DEBUG("s_CurrentAPI is NULL \n");    
    }
    
    // DEBUG("Calling... SetupTextureInfo \n");
    
    // s_CurrentAPI->SetupTextureInfo(Width, Height, Hwnd);
    
    DEBUG("Calling... ProcessDeviceEvent \n");
    
    s_CurrentAPI->ProcessDeviceEvent(kUnityGfxDeviceEventInitialize, s_UnityInterfaces);

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

extern "C" void* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getVideoFrameVLC (libvlc_media_player_t* mp, bool * updated)
{
    if(mp == NULL)
        return nullptr;

    RenderAPI* s_CurrentAPI = contexts.find(mp)->second;

    if (!s_CurrentAPI) {
        DEBUG("Error, no Render API");
        if (updated)
            *updated = false;
        return nullptr;
    }

    return s_CurrentAPI->getVideoFrame(updated);
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    DEBUG("UnityPluginLoad");
    s_UnityInterfaces = unityInterfaces;
    s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
    s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

    // Run OnGraphicsDeviceEvent(initialize) manually on plugin load
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
  s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
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

        s_DeviceType = s_Graphics->GetRenderer();
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
            currentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
        }
    }
}

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
}

// void TextureUpdateCallback(int eventID, void* data)
// {
//     DEBUG("Entering TextureUpdateCallback \n");
//     auto event = static_cast<UnityRenderingExtEventType>(eventID);

//     if (event == kUnityRenderingExtEventUpdateTextureBeginV2)
//     {
//         DEBUG("event is kUnityRenderingExtEventUpdateTextureBeginV2 \n");

//         auto *pParams = reinterpret_cast<UnityRenderingExtTextureUpdateParamsV2*>(data);

//         if(mp == NULL)
//             return;

//         RenderAPI* s_CurrentAPI = contexts.find(mp)->second;

//         bool* updated;

//         if (!s_CurrentAPI) {
//             DEBUG("Error, no Render API \n");
//             if (updated)
//                 *updated = false;
//             return;
//         }

//         DEBUG("calling s_CurrentAPI->getVideoFrame(updated) \n");

//         void* tex = s_CurrentAPI->getVideoFrame(updated);
        
//         DEBUG("SWAPPING -=================== \n");
//         pParams->format = kUnityRenderingExtFormatR8G8B8A8_UNorm;

//         std::swap(tex, pParams->texData); 
//     }
//     else if (event == kUnityRenderingExtEventUpdateTextureEndV2)
//     {     
//         DEBUG("event is kUnityRenderingExtEventUpdateTextureEndV2 \n");
//         auto *pParams = reinterpret_cast<UnityRenderingExtTextureUpdateParamsV2*>(data);
//         if(pParams->texData != nullptr)
//         {
//             pParams->texData = nullptr;
//         }
//     }
// }

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
    return OnRenderEvent;
}

// extern "C" UnityRenderingEventAndData UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetTextureUpdateCallback()
// {
//     return TextureUpdateCallback;
// }