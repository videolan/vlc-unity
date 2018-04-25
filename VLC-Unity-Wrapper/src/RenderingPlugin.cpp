// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Log.h"

extern "C"
{
#include <stdlib.h>
#include <unistd.h>
#include <vlc/vlc.h>
#include <string.h>

}

static RenderAPI* s_CurrentAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;
static int   g_TextureRowPitch = 0;

libvlc_instance_t * inst;
libvlc_media_player_t *mp;
libvlc_media_t *m;

/** LibVLC's API function exported to Unity
 *
 * Every following functions will be exported to. Unity We have to
 * redeclare the LibVLC's function for the keyword
 * UNITY_INTERFACE_EXPORT and UNITY_INTERFACE_API
 */

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
stopVLC () {
    if ( mp ) {
        // Stop playing
        libvlc_media_player_stop (mp);

        // Free the media_player
        libvlc_media_player_release (mp);
        mp = NULL;
    }

    if (m) {
        libvlc_media_release( m );
        m = NULL;
    }

    DEBUG("VLC STOPPED");
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
initVLC(const char** psz_extra_options, int i_extra_options)
{
    const char *psz_default_options[] = {
#if UNITY_ANDROID
        "--codec=mediacodec_ndk,all",
#endif
        "--no-lua"
    };
    int i_defaults_options = sizeof(psz_default_options) / sizeof(*psz_default_options);

    int i_all_options = i_defaults_options + i_extra_options;
    const char** psz_all_options = (const char**)malloc( i_all_options * sizeof(char**) );

    for (int i = 0 ; i < i_defaults_options; i++)
        psz_all_options[i] = psz_default_options[i];
    for (int i = 0 ; i < i_extra_options; i++)
        psz_all_options[i_defaults_options + i] = psz_extra_options[i];

    // Create an instance of LibVLC
    DEBUG("Instantiating LibLVC : %s...", libvlc_get_version());

    if (!inst)
    {
        for (int i = 0 ; i < i_all_options; i++)
            DEBUG("VLC options: %s", psz_all_options[i]);

        inst = libvlc_new(i_all_options, psz_all_options);
    }

    if (inst == NULL) {
        DEBUG("Error instantiating LibVLC");
        return;
    }
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
disposeVLC(const char* psz_options, int i_options)
{
    DEBUG("Dispose LibVLC");
    stopVLC();
    if ( inst )
    {
        libvlc_release( inst );
        inst = nullptr;
    }
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
playVLC (char *videoURL)
{
    DEBUG("LAUNCH");
    if (!s_CurrentAPI) {
        DEBUG("Error, no Render API");
        return;
    }

    if (inst == NULL) {
        DEBUG("LibVLC is not instanciated");
        return;
    }

    // Create a new item
    DEBUG("Video url : %s", videoURL);
    m = libvlc_media_new_location (inst, videoURL);
    if (m == NULL) {
        DEBUG("Error initializing media");
        goto err;
    }

    mp = libvlc_media_player_new_from_media (m);
    if (mp == NULL) {
        DEBUG("Error initializing media player");
        goto err;
    }

    DEBUG("setVlcContext s_CurrentAPI=%p mp=%p", s_CurrentAPI, mp);
    s_CurrentAPI->setVlcContext(mp);

    DEBUG("play");

    // Play the media
    libvlc_media_player_play (mp);
    return;

err:
    stopVLC();
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
playPauseVLC ()
{
    if (! mp)
        return ;

    // Pause playing
    libvlc_media_player_pause (mp);
    DEBUG("VLC PAUSE TOGGLED");
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
pauseVLC ()
{
    if (! mp)
        return ;

    // Paused playing
    libvlc_media_player_pause (mp);
    DEBUG("VLC PAUSED");
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getLengthVLC ()
{
    if (! mp)
        return -1;
    DEBUG( "Length %d", (int) libvlc_media_player_get_length (mp));
    return (int) libvlc_media_player_get_length (mp);
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getTimeVLC ()
{
    if (! mp)
        return -1;
    return (int) libvlc_media_player_get_time (mp);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
setTimeVLC (int pos)
{
    if (! mp)
        return;

    libvlc_media_player_set_time (mp, pos);
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getVideoHeightVLC ()
{
    if (! mp)
        return -1;

    unsigned int w, h;
    if(libvlc_video_get_size (mp, 0, &w, &h) == -1)
        return 0;
    DEBUG("getVideoHeightVLC %u", h);
    return h;
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getVideoWidthVLC ()
{
    if (! mp)
        return -1;

    unsigned int w, h;
    if(libvlc_video_get_size (mp, 0, &w, &h) == -1)
        return 0;
    DEBUG("getVideoWidthVLC %u", w);
    return w;
}


extern "C" void* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getVideoFrameVLC (bool * updated)
{
    if (!s_CurrentAPI) {
        DEBUG("Error, no Render API");
        if (updated)
            *updated = false;
        return nullptr;
    }

    return s_CurrentAPI->getVideoFrame(updated);
}

/** Unity API function
 *
 * Following functions are needed for integration into Unity's API.
 * UnitySetInterfaces
 */

// --------------------------------------------------------------------------
//  UnitySetInterfaces

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;

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

#if UNITY_WEBGL
typedef void	(UNITY_INTERFACE_API * PluginLoadFunc)(IUnityInterfaces* unityInterfaces);
typedef void	(UNITY_INTERFACE_API * PluginUnloadFunc)();

extern "C" void	UnityRegisterRenderingPlugin(PluginLoadFunc loadPlugin, PluginUnloadFunc unloadPlugin);

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RegisterPlugin()
{
    UnityRegisterRenderingPlugin(UnityPluginLoad, UnityPluginUnload);
}
#endif


// --------------------------------------------------------------------------
// GraphicsDeviceEvent

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
    // Create graphics API implementation upon initialization
    if (eventType == kUnityGfxDeviceEventInitialize) {
        DEBUG("Initialise Render API");
        if (s_CurrentAPI != NULL) {
            DEBUG("*** s_CurrentAPI != NULL while initialising ***");
            return;
        }
        s_DeviceType = s_Graphics->GetRenderer();
        s_CurrentAPI = CreateRenderAPI(s_DeviceType);
    }

    // Let the implementation process the device related events
    if (s_CurrentAPI) {
        s_CurrentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
    } else {
        DEBUG("Unable to process event, no Render API");
    }

    // Cleanup graphics API implementation upon shutdown
    if (eventType == kUnityGfxDeviceEventShutdown) {
        DEBUG("Destroy Render API");
        delete s_CurrentAPI;
        s_CurrentAPI = NULL;
        s_DeviceType = kUnityGfxRendererNull;
    }
}


// --------------------------------------------------------------------------
// OnRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{

    // Unknown / unsupported graphics device type? Do nothing
    if (s_CurrentAPI == NULL) {
        DEBUG("OnRenderEvent no API");
        return;
    }
}


// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
    return OnRenderEvent;
}
