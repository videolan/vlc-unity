#pragma once

// Standard base includes, defines that indicate our current platform, etc.

#include <stddef.h>


// Which platform we are on?
// UNITY_WIN - Windows (regular win32)
// UNITY_OSX - Mac OS X
// UNITY_LINUX - Linux
// UNITY_IPHONE - iOS
// UNITY_ANDROID - Android
// UNITY_METRO - WSA or UWP
// UNITY_WEBGL - WebGL
#if defined(_WIN32)
	#define UNITY_WIN 1
#elif defined(__APPLE__)
	#import <TargetConditionals.h>
	#if TARGET_OS_IPHONE
		#define UNITY_IPHONE 1
	#else
		#define UNITY_OSX 1
	#endif
#elif defined(UNITY_METRO) || defined(UNITY_ANDROID) || defined(UNITY_LINUX) || defined(UNITY_WEBGL)
	// these are defined externally
#elif defined(__EMSCRIPTEN__)
	// this is already defined in Unity 5.6
	#define UNITY_WEBGL 1
#else
	#error "Unknown platform!"
#endif



// Which graphics device APIs we possibly support?
#if defined(UNITY_METRO)
	#define SUPPORT_D3D11 1
	#if defined(WINDOWS_UWP)
		#define SUPPORT_D3D12 1
	#endif
#elif defined(UNITY_WIN)
	#define SUPPORT_D3D9 1
	#define SUPPORT_D3D11 1 // comment this out if you don't have D3D11 header/library files
	#define SUPPORT_D3D12 0 //@TODO: enable by default? comment this out if you don't have D3D12 header/library files
	#define SUPPORT_OPENGL_LEGACY 1
	#define SUPPORT_OPENGL_UNIFIED 1
	#define SUPPORT_OPENGL_CORE 1
#elif defined(UNITY_IPHONE) || defined(UNITY_ANDROID) || defined(UNITY_WEBGL)
	#define SUPPORT_OPENGL_UNIFIED 1
	#define SUPPORT_OPENGL_ES 1
#elif defined(UNITY_OSX) || defined(UNITY_LINUX)
	#define SUPPORT_OPENGL_LEGACY 1
	#define SUPPORT_OPENGL_UNIFIED 1
	#define SUPPORT_OPENGL_CORE 1
#endif

#if defined(UNITY_IPHONE) || defined(UNITY_OSX)
	#define SUPPORT_METAL 1
#endif



// COM-like Release macro
#ifndef SAFE_RELEASE
	#define SAFE_RELEASE(a) if (a) { a->Release(); a = NULL; }
#endif

