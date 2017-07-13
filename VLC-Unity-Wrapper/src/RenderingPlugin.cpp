// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"

#include <assert.h>
#include <math.h>
#include <vector>

extern "C"
{
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <vlc/vlc.h>
}

#define VIDEOFILE "file:///home/nimag42/VLC/VLC-Virtual-Cinema/Assets/Movies/Antman.mkv" // The movie to read


// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.
static RenderAPI* s_CurrentAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static float g_Time;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity (float t) { g_Time = t; }


// --------------------------------------------------------------------------
// SetTextureFromUnity, an example function we export which is called by one of the scripts.

static void* g_TextureHandle = NULL;
static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;
static int   g_TextureRowPitch = 0;

static unsigned char* vlcVideoFramePtr = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* lockfct
 * Callback for LibVLC to lock data before decoding
 */
extern "C" void *
lockfct (void *data, void **p_pixels)
{
  // Lock the mutex to ensure data safeness
  pthread_mutex_lock(&mutex);

  free(vlcVideoFramePtr);
  vlcVideoFramePtr = (unsigned char *) malloc (g_TextureWidth * g_TextureHeight * 4);
  *p_pixels = vlcVideoFramePtr;

  //fprintf(stderr, "[CUSTOMVLC] LOCK FUNCTION Called   : Texture PTR: %p, DataPTR: %p\n", g_TextureHandle, *p_pixels);

  return NULL;
}

/* unlockfct
 * Callback for LibVLC to release datas
 */
extern "C" void
unlockfct (void *data, void *id, void * const *p_pixels)
{
  // Release datas
  pthread_mutex_unlock(&mutex);
}


static void
ModifyTexturePixels ()
{
  void* textureHandle = g_TextureHandle;
  int width = g_TextureWidth;
  int height = g_TextureHeight;
  if (!textureHandle)
    return;
    
  // Lock mutex to ensure all datas had been written
  pthread_mutex_lock(&mutex);
    
  int textureRowPitch;
  void* textureDataPtr = s_CurrentAPI->BeginModifyTexture(textureHandle, width, height, &textureRowPitch);
  if (!textureDataPtr)
    return;
    
  //fprintf(stderr, "[CUSTOMVLC] MODIFY TEXTURE Called  : Texture PTR: %p, DataPTR: %p\n", textureHandle, textureDataPtr);

  unsigned char* ptr = (unsigned char *) textureDataPtr;
  unsigned char* origin = (unsigned char *) vlcVideoFramePtr;
    
  for (int i = 0; i < height*width*4; ++i)
    {
      *ptr = *origin;
      ++ptr;
      ++origin;
    }

  s_CurrentAPI->EndModifyTexture(textureHandle, width, height, textureRowPitch, textureDataPtr);

  // Release datas
  pthread_mutex_unlock(&mutex);
}


extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
SetTextureFromUnity (void* textureHandle, int w, int h)
{
  // A script calls this at initialization time; just remember the texture pointer here.
  // Will update texture pixels each frame from the plugin rendering event (texture update
  // needs to happen on the rendering thread).
  g_TextureHandle = textureHandle;
  g_TextureWidth = w;
  g_TextureHeight = h;
}

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
launchVLC ()
{
  // Create a mutex, to share data between LibVLC's callback and Unity
  fprintf (stderr, "[CUSTOMVLC] Instantiating mutex...\n");
  if (pthread_mutex_init (&mutex, NULL) != 0)
    fprintf(stderr, "[CUSTOMVLC] Mutex init failed\n");

  // Create an instance of LibVLC
  fprintf(stderr, "[CUSTOMVLC] Instantiating LibLVC...\n");
  inst = libvlc_new (0, NULL);

  // Create a new item
  m = libvlc_media_new_location (inst, VIDEOFILE);
  mp = libvlc_media_player_new_from_media (m);

  // Release the media and the player since we don't need them anymore
  libvlc_media_release (m);
  libvlc_release(inst);

  // Instantiate a first buffer from frame, because our callbacks will
  // try to free it
  vlcVideoFramePtr = (unsigned char *) malloc (g_TextureWidth * g_TextureHeight * 4);

  // Set callbacks for activating vmem. Vmem let us handle video output
  // separatly from LibVLC classical way
  libvlc_video_set_callbacks(mp, lockfct, unlockfct, NULL, NULL);
  libvlc_video_set_format(mp, "RV32", g_TextureWidth, g_TextureHeight, g_TextureWidth*4);

  // Play the media
  libvlc_media_player_play (mp);
}	

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
stopVLC () {
  // Stop playing
  libvlc_media_player_stop (mp);
 
  // Free the media_player
  libvlc_media_player_release (mp);

  fprintf(stderr, "[CUSTOMVLC] VLC STOPPED\n");
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
playPauseVLC ()
{
  // Pause playing
  libvlc_media_player_pause (mp);
 
  fprintf(stderr, "[CUSTOMVLC] VLC PAUSE TOGGLED\n");
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
pauseVLC ()
{
  // Paused playing
  libvlc_media_player_pause (mp);
 
  fprintf(stderr, "[CUSTOMVLC] VLC PAUSED\n");
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getLengthVLC ()
{
  fprintf(stderr, "[CUSTOMVLC] Length %d", (int) libvlc_media_player_get_length (mp));
  return (int) libvlc_media_player_get_length (mp);
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
getTimeVLC ()
{
  return (int) libvlc_media_player_get_time (mp);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
setTimeVLC (int pos)
{
    libvlc_media_player_set_time (mp, pos);
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
  if (eventType == kUnityGfxDeviceEventInitialize)
    {
      assert(s_CurrentAPI == NULL);
      s_DeviceType = s_Graphics->GetRenderer();
      s_CurrentAPI = CreateRenderAPI(s_DeviceType);
    }

  // Let the implementation process the device related events
  if (s_CurrentAPI)
    {
      s_CurrentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
    }

  // Cleanup graphics API implementation upon shutdown
  if (eventType == kUnityGfxDeviceEventShutdown)
    {
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
  if (s_CurrentAPI == NULL)
    return;

  ModifyTexturePixels();
}


// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
  return OnRenderEvent;
}

