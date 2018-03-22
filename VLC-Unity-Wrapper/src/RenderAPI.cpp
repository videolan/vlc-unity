#include "RenderAPI.h"
#include "PlatformBase.h"
#include "Unity/IUnityGraphics.h"


RenderAPI* CreateRenderAPI(UnityGfxRenderer apiType)
{
#	if SUPPORT_D3D11
	if (apiType == kUnityGfxRendererD3D11)
	{
		extern RenderAPI* CreateRenderAPI_D3D11();
		return CreateRenderAPI_D3D11();
	}
#	endif // if SUPPORT_D3D11

//#	if SUPPORT_D3D9
//	if (apiType == kUnityGfxRendererD3D9)
//	{
//		extern RenderAPI* CreateRenderAPI_D3D9();
//		return CreateRenderAPI_D3D9();
//	}
//#	endif // if SUPPORT_D3D9
//
//#	if SUPPORT_D3D12
//	if (apiType == kUnityGfxRendererD3D12)
//	{
//		extern RenderAPI* CreateRenderAPI_D3D12();
//		return CreateRenderAPI_D3D12();
//	}
//#	endif // if SUPPORT_D3D9


#	if SUPPORT_OPENGL_UNIFIED
	if (apiType == kUnityGfxRendererOpenGLCore || apiType == kUnityGfxRendererOpenGLES20 || apiType == kUnityGfxRendererOpenGLES30)
	{
#if UNITY_LINUX
		extern RenderAPI* CreateRenderAPI_OpenGLX(UnityGfxRenderer apiType);
		return CreateRenderAPI_OpenGLX(apiType);
#elif UNITY_WIN
		extern RenderAPI* CreateRenderAPI_OpenWGL(UnityGfxRenderer apiType);
		return CreateRenderAPI_OpenWGL(apiType);
#else
        return NULL;
#endif
	}
#	endif // if SUPPORT_OPENGL_UNIFIED

//#	if SUPPORT_OPENGL_LEGACY
//	if (apiType == kUnityGfxRendererOpenGL)
//	{
//		extern RenderAPI* CreateRenderAPI_OpenGL2();
//		return CreateRenderAPI_OpenGL2();
//	}
//#	endif // if SUPPORT_OPENGL_LEGACY

#	if SUPPORT_METAL
	if (apiType == kUnityGfxRendererMetal)
	{
		extern RenderAPI* CreateRenderAPI_Metal();
		return CreateRenderAPI_Metal();
	}
#	endif // if SUPPORT_METAL


	// Unknown or unsupported graphics API
	return NULL;
}
