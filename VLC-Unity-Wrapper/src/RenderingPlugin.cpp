// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"

#include <assert.h>
#include <math.h>
#include <vector>

#include "GLEW/glew.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>

extern "C"
{
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <vlc/vlc.h>
}

static RenderAPI* s_CurrentAPI = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static float g_Time;

static GLuint g_TextureHandle = 0;
static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;
static int   g_TextureRowPitch = 0;

static unsigned char* vlcVideoFramePtr = NULL;

GLuint bufferTexture;

Display * dpy;
GLXContext unityGLContext;

// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity (float t) { g_Time = t; }


extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
SetTextureFromUnity (void* textureHandle, int w, int h)
{
  // A script calls this at initialization time; just remember the texture pointer here.
  // Will update texture pixels each frame from the plugin rendering event (texture update
  // needs to happen on the rendering thread).
  g_TextureHandle = (GLuint) (size_t) textureHandle;
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
launchVLC (char *videoURL)
{

  dpy = XOpenDisplay(NULL);
  char display[128];
  if (snprintf(display, sizeof(display), "%ld", (long)dpy) < 0)
    {
      printf("Could not write the display variable\n");
      exit(1);
    }

  unityGLContext = glXGetCurrentContext();
  char glx_context[128];
  if (snprintf(glx_context, sizeof(glx_context), "%ld", (long)unityGLContext) < 0)
    {
      printf("Could not write the context variable\n");
      exit(1);
    }

  char textureId[128];
  if (snprintf(textureId, sizeof(textureId), "%d", (GLuint) g_TextureHandle) < 0)
    {
      printf("Could not write the framebuffer Id\n");
      exit(1);
    }

  const char *args[] = {"--vout", "gl",
			"--glx-display", display,
			"--glx-context", glx_context,
			"--glx-texture", textureId,
			"--avcodec-hw", "none"};
  // Create an instance of LibVLC
  fprintf(stderr, "[LIBVLC] Instantiating LibLVC : %s...\n", libvlc_get_version());
  inst = libvlc_new(sizeof(args) / sizeof(*args), args);
  if (inst == NULL)
    fprintf(stderr, "[LIBVLC] Error instantiating LibVLC\n");


  // Create a new item
  fprintf(stderr, "[LIBVLC] Video url : %s\n", videoURL);
  m = libvlc_media_new_location (inst, videoURL);
  if (m == NULL)
    fprintf(stderr, "[LIBVLC] Error initializing media\n");
  mp = libvlc_media_player_new_from_media (m);
  if (mp == NULL)
    fprintf(stderr, "[LIBVLC] Error initializing media player\n");

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

  // ModifyTexturePixels();
}


// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
  return OnRenderEvent;
}

