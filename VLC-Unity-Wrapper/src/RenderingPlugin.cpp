// Example low level rendering Unity plugin

#include "PlatformBase.h"
#include "RenderAPI.h"

#include <assert.h>
#include <math.h>
#include <vector>

#include "GLEW/glew.h"
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

static void* g_TextureHandle = NULL;
static int   g_TextureWidth  = 0;
static int   g_TextureHeight = 0;
static int   g_TextureRowPitch = 0;

static unsigned char* vlcVideoFramePtr = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

GLuint bufferTexture;
GLuint fboId;


void debugImage(unsigned char * beginning, int nbPixels)
{
  for (unsigned char *ptr = beginning + g_TextureWidth*g_TextureHeight; ptr < beginning + g_TextureWidth*g_TextureHeight + 4*nbPixels; ptr++)
    {
      fprintf (stderr, "%x", *ptr);
    }
  fprintf (stderr, "\n");
}

void debugTexture(GLuint texture, int nbPixels)
{
  unsigned char *check = (unsigned char *) malloc (g_TextureWidth*g_TextureHeight*4);
  glBindTexture(GL_TEXTURE_2D, texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, check);
  debugImage(check, nbPixels);
}

void createTexture() {
  glGenTextures(1, &bufferTexture);
  fprintf(stderr, "1: %d ; ", glGetError());
  glBindTexture(GL_TEXTURE_2D, bufferTexture);
  fprintf(stderr, "2: %d ; ", glGetError());

  // glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  // fprintf(stderr, "3: %d ; ", glGetError());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  fprintf(stderr, "4: %d ; ", glGetError());
  // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  // fprintf(stderr, "5: %d ; ", glGetError());
  // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
		  // GL_CLAMP_TO_EDGE);
  // fprintf(stderr, "6: %d ; ", glGetError());
  // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
		  // GL_CLAMP_TO_EDGE);
  // fprintf(stderr, "7: %d ; ", glGetError());
  // glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  // fprintf(stderr, "8: %d ; ", glGetError());

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_TextureWidth, g_TextureHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
  fprintf(stderr, "9: %d", glGetError());
}

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

// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity (float t) { g_Time = t; }

// --------------------------------------------------------------------------
// ModifyTexturePixels, an example function we export which is called by one of the scripts.

static void
ModifyTexturePixels ()
{
  void* textureHandle = g_TextureHandle;
  int width = g_TextureWidth;
  int height = g_TextureHeight;
  GLuint gltex = (GLuint)(size_t)(textureHandle);
  if (!gltex)
    return;
    
  // Lock mutex to ensure all datas had been written
  pthread_mutex_lock(&mutex);
    
  // int textureRowPitch;
  // void* textureDataPtr = s_CurrentAPI->BeginModifyTexture(textureHandle, width, height, &textureRowPitch);
  // if (!textureDataPtr)
    // return;

  //fprintf(stderr, "[CUSTOMVLC] MODIFY TEXTURE Called  : Texture PTR: %p, DataPTR: %p\n", textureHandle, textureDataPtr);

  /*
  unsigned char* ptr = (unsigned char *) textureDataPtr;
  unsigned char* origin = (unsigned char *) vlcVideoFramePtr;
    
  for (int i = 0; i < height*width*4; ++i)
    {
      *ptr = *origin;
      ++ptr;
      ++origin;
    }
  */

  // s_CurrentAPI->EndModifyTexture(textureHandle, width, height, textureRowPitch, vlcVideoFramePtr);

  /********************* UGLY TESTING HERE **************************/

  /****** Copy to intermediate buffer (simulate vbridge) */
  fprintf(stderr, "\n[LIBVLC] Rendering to intermediate buffer :\n");

  fprintf(stderr, "1: %d ; ", glGetError());
  glBindTexture(GL_TEXTURE_2D, bufferTexture);
  fprintf(stderr, "2: %d ; ", glGetError());
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_TextureWidth, g_TextureHeight, GL_RGBA, GL_UNSIGNED_BYTE, vlcVideoFramePtr);
  fprintf(stderr, "3: %d ; ", glGetError());
  //debugTexture(bufferTexture, 100);

  /****** in-GPU copy from intermediate buffer to Unity's */
  fprintf(stderr, "\n[LIBVLC] In-GPU Copy :\n");

  // We need to bind to a specific FBO to copy the texture
  glBindFramebuffer(GL_FRAMEBUFFER, fboId);

  // // Test various stuff to debug
  // fprintf(stderr, "Texture %d exists: %d  ", bufferTexture, glIsTexture(bufferTexture));
  // fprintf(stderr, "Texture %d exists: %d  ", textureHandle, glIsTexture(gltex));
  // fprintf(stderr, "FBO status: %d ; ", glCheckFramebufferStatus(GL_READ_FRAMEBUFFER));
  // GLint result; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &result);
  // fprintf(stderr, "READ_FBO bound to %d  ", (int) result);


  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bufferTexture, 0);
  fprintf(stderr, "1: %s ; ", gluErrorString(glGetError()));
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  fprintf(stderr, "2: %s ; ", gluErrorString(glGetError()));
  glBindTexture(GL_TEXTURE_2D, gltex);
  fprintf(stderr, "3: %s ; ", gluErrorString(glGetError()));
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
  fprintf(stderr, "4: %s ; ", gluErrorString(glGetError()));

  // Rebing to default FBO
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
launchVLC (char *videoURL)
{
  // Create an FBO
  glGenFramebuffers(1, &fboId);

  // Create temporary texture
  fprintf(stderr, "\n\n[LIBVLC] Create texture :\n");
  createTexture();

  // Create a mutex, to share data between LibVLC's callback and Unity
  fprintf (stderr, "[LIBVLC] Instantiating mutex...\n");
  if (pthread_mutex_init (&mutex, NULL) != 0)
    fprintf(stderr, "[LIBVLC] Mutex init failed\n");

  // Create an instance of LibVLC
  fprintf(stderr, "[LIBVLC] Instantiating LibLVC : %s...\n", libvlc_get_version());
  inst = libvlc_new (0, NULL);
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

