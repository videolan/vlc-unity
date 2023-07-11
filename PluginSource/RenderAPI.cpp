#include "RenderAPI.h"
#include "PlatformBase.h"
#include "Unity/IUnityGraphics.h"


RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType)
{
#if defined(SUPPORT_D3D11)
    if (apiType == kUnityGfxRendererD3D11)
    {
        extern RenderAPI* CreateRenderAPI_D3D11();
        return CreateRenderAPI_D3D11();
    }
#endif // if SUPPORT_D3D11

#if defined(SUPPORT_OPENGL_UNIFIED)
	if (apiType == kUnityGfxRendererOpenGLCore || apiType == kUnityGfxRendererOpenGLES20 || apiType == kUnityGfxRendererOpenGLES30)
	{
#if defined(UNITY_ANDROID)
        extern RenderAPI* CreateRenderAPI_Android(UnityGfxRenderer apiType);
		return CreateRenderAPI_Android(apiType);
#endif
	}
#endif // if SUPPORT_OPENGL_UNIFIED

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
