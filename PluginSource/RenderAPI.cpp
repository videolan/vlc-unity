#include "RenderAPI.h"
#include "Log.h"
#include "PlatformBase.h"
#include "Unity/IUnityGraphics.h"

#if defined(UNITY_LINUX)
#include "LinuxGraphicsInterop.h"
#include <cstdlib>
#endif


RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType)
{
#if defined(SUPPORT_D3D11) || defined(SUPPORT_D3D12)
    if (apiType == kUnityGfxRendererD3D11 || apiType == kUnityGfxRendererD3D12)
    {
        extern RenderAPI* CreateRenderAPI_D3D11(UnityGfxRenderer apiType);
        return CreateRenderAPI_D3D11(apiType);
    }
#endif

#if defined(SUPPORT_OPENGL_UNIFIED)
	if (apiType == kUnityGfxRendererOpenGLCore || apiType == kUnityGfxRendererOpenGLES20 || apiType == kUnityGfxRendererOpenGLES30)
	{
#if defined(UNITY_ANDROID)
        extern RenderAPI* CreateRenderAPI_Android(UnityGfxRenderer apiType);
		return CreateRenderAPI_Android(apiType);
#elif defined(UNITY_LINUX)
        {
            const char* backendOverride = getenv("VLC_UNITY_LINUX_OPENGL_BACKEND");
            const LinuxOpenGLBackend backend = LinuxChooseOpenGLBackend(
                backendOverride, getenv("DISPLAY"), getenv("WAYLAND_DISPLAY"));
            if (!LinuxIsOpenGLBackendOverrideValid(backendOverride)) {
                DEBUG("[Linux] ignoring invalid VLC_UNITY_LINUX_OPENGL_BACKEND=%s",
                      backendOverride);
            }
            DEBUG("[Linux] selected %s OpenGL interop backend (DISPLAY=%s, WAYLAND_DISPLAY=%s)",
                  LinuxOpenGLBackendName(backend),
                  getenv("DISPLAY") ? getenv("DISPLAY") : "",
                  getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "");
            if (backend == LinuxOpenGLBackend::EGL) {
                extern RenderAPI* CreateRenderAPI_OpenGLLinuxEGL(UnityGfxRenderer apiType);
                return CreateRenderAPI_OpenGLLinuxEGL(apiType);
            }
        }
        extern RenderAPI* CreateRenderAPI_OpenGLGLX(UnityGfxRenderer apiType);
        return CreateRenderAPI_OpenGLGLX(apiType);
#endif
	}
#endif // if SUPPORT_OPENGL_UNIFIED

#if defined(SUPPORT_VULKAN)
    if (apiType == kUnityGfxRendererVulkan)
    {
        extern RenderAPI* CreateRenderAPI_Vulkan(UnityGfxRenderer apiType);
        return CreateRenderAPI_Vulkan(apiType);
    }
#endif

#if defined(UNITY_OSX)
    extern RenderAPI* CreateRenderAPI_OpenGLCGL(UnityGfxRenderer apiType);
    if (apiType == kUnityGfxRendererMetal)
        return CreateRenderAPI_OpenGLCGL(apiType);
#endif

#if defined(UNITY_IPHONE)
    extern RenderAPI* CreateRenderAPI_OpenGLEAGL(UnityGfxRenderer apiType);
    if (apiType == kUnityGfxRendererMetal)
        return CreateRenderAPI_OpenGLEAGL(apiType);
#endif

    // Unknown or unsupported graphics API
    return NULL;
}
