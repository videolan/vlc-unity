#include "RenderAPI.h"
#include "PlatformBase.h"
#include "Unity/IUnityGraphics.h"


RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType)
{
    // if (apiType == kUnityGfxRendererD3D11)
    // {
        extern RenderAPI* CreateRenderAPI_D3D11();
        return CreateRenderAPI_D3D11();
    // }

// #	if SUPPORT_D3D9
// 	if (apiType == kUnityGfxRendererD3D9)
// 	{
// 		extern RenderAPI* CreateRenderAPI_D3D9();
// 		return CreateRenderAPI_D3D9();
// 	}
// #	endif // if SUPPORT_D3D9

// #	if SUPPORT_D3D12
// 	if (apiType == kUnityGfxRendererD3D12)
// 	{
// 		extern RenderAPI* CreateRenderAPI_D3D12();
// 		return CreateRenderAPI_D3D12();
// 	}
// #	endif // if SUPPORT_D3D9


// #	if SUPPORT_OPENGL_UNIFIED
// 	if (apiType == kUnityGfxRendererOpenGLCore || apiType == kUnityGfxRendererOpenGLES20 || apiType == kUnityGfxRendererOpenGLES30)
// 	{
// #if UNITY_LINUX
// 		extern RenderAPI* CreateRenderAPI_OpenGLX(UnityGfxRenderer apiType);
// 		return CreateRenderAPI_OpenGLX(apiType);
// #elif UNITY_WIN
// 		extern RenderAPI* CreateRenderAPI_OpenWGL(UnityGfxRenderer apiType);
// 		return CreateRenderAPI_OpenWGL(apiType);
// #elif UNITY_ANDROID
//         extern RenderAPI* CreateRenderAPI_Android(UnityGfxRenderer apiType);
// 		return CreateRenderAPI_Android(apiType);
// #else
//         return NULL;
// #endif
// 	}
// #	endif // if SUPPORT_OPENGL_UNIFIED

    // Unknown or unsupported graphics API
    return NULL;
}
