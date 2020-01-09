#pragma once
#include "IUnityInterface.h"
#include <cstdint>

enum UnityRenderingExtEventType
{
    kUnityRenderingExtEventSetStereoTarget,                 // issued during SetStereoTarget and carrying the current 'eye' index as parameter
    kUnityRenderingExtEventSetStereoEye,                    // issued during stereo rendering at the beginning of each eye's rendering loop. It carries the current 'eye' index as parameter
    kUnityRenderingExtEventStereoRenderingDone,             // issued after the rendering has finished
    kUnityRenderingExtEventBeforeDrawCall,                  // issued during BeforeDrawCall and carrying UnityRenderingExtBeforeDrawCallParams as parameter
    kUnityRenderingExtEventAfterDrawCall,                   // issued during AfterDrawCall. This event doesn't carry any parameters
    kUnityRenderingExtEventCustomGrab,                      // issued during GrabIntoRenderTexture since we can't simply copy the resources
                                                            //      when custom rendering is used - we need to let plugin handle this. It carries over
                                                            //      a UnityRenderingExtCustomBlitParams params = { X, source, dest, 0, 0 } ( X means it's irrelevant )
    kUnityRenderingExtEventCustomBlit,                      // issued by plugin to insert custom blits. It carries over UnityRenderingExtCustomBlitParams as param.
    kUnityRenderingExtEventUpdateTextureBegin,                                                  // Deprecated.
    kUnityRenderingExtEventUpdateTextureEnd,                                                    // Deprecated.
    kUnityRenderingExtEventUpdateTextureBeginV1 = kUnityRenderingExtEventUpdateTextureBegin,    // Deprecated. Issued to update a texture. It carries over UnityRenderingExtTextureUpdateParamsV1
    kUnityRenderingExtEventUpdateTextureEndV1 = kUnityRenderingExtEventUpdateTextureEnd,        // Deprecated. Issued to signal the plugin that the texture update has finished. It carries over the same UnityRenderingExtTextureUpdateParamsV1 as kUnityRenderingExtEventUpdateTextureBeginV1
    kUnityRenderingExtEventUpdateTextureBeginV2,                                                // Issued to update a texture. It carries over UnityRenderingExtTextureUpdateParamsV2
    kUnityRenderingExtEventUpdateTextureEndV2,                                                  // Issued to signal the plugin that the texture update has finished. It carries over the same UnityRenderingExtTextureUpdateParamsV2 as kUnityRenderingExtEventUpdateTextureBeginV2

    // keep this last
    kUnityRenderingExtEventCount,
    kUnityRenderingExtUserEventsStart = kUnityRenderingExtEventCount
};

typedef enum UnityGfxRenderer
{
	kUnityGfxRendererOpenGL            =  0, // Legacy OpenGL
	kUnityGfxRendererD3D9              =  1, // Direct3D 9
	kUnityGfxRendererD3D11             =  2, // Direct3D 11
	kUnityGfxRendererGCM               =  3, // PlayStation 3
	kUnityGfxRendererNull              =  4, // "null" device (used in batch mode)
	kUnityGfxRendererXenon             =  6, // Xbox 360
	kUnityGfxRendererOpenGLES20        =  8, // OpenGL ES 2.0
	kUnityGfxRendererOpenGLES30        = 11, // OpenGL ES 3.x
	kUnityGfxRendererGXM               = 12, // PlayStation Vita
	kUnityGfxRendererPS4               = 13, // PlayStation 4
	kUnityGfxRendererXboxOne           = 14, // Xbox One
	kUnityGfxRendererMetal             = 16, // iOS Metal
	kUnityGfxRendererOpenGLCore        = 17, // OpenGL core
	kUnityGfxRendererD3D12             = 18, // Direct3D 12
} UnityGfxRenderer;

typedef enum UnityGfxDeviceEventType
{
	kUnityGfxDeviceEventInitialize     = 0,
	kUnityGfxDeviceEventShutdown       = 1,
	kUnityGfxDeviceEventBeforeReset    = 2,
	kUnityGfxDeviceEventAfterReset     = 3,
} UnityGfxDeviceEventType;

typedef void (UNITY_INTERFACE_API * IUnityGraphicsDeviceEventCallback)(UnityGfxDeviceEventType eventType);

// Should only be used on the rendering thread unless noted otherwise.
UNITY_DECLARE_INTERFACE(IUnityGraphics)
{
	UnityGfxRenderer (UNITY_INTERFACE_API * GetRenderer)(); // Thread safe

	// This callback will be called when graphics device is created, destroyed, reset, etc.
	// It is possible to miss the kUnityGfxDeviceEventInitialize event in case plugin is loaded at a later time,
	// when the graphics device is already created.
	void (UNITY_INTERFACE_API * RegisterDeviceEventCallback)(IUnityGraphicsDeviceEventCallback callback);
	void (UNITY_INTERFACE_API * UnregisterDeviceEventCallback)(IUnityGraphicsDeviceEventCallback callback);
};
UNITY_REGISTER_INTERFACE_GUID(0x7CBA0A9CA4DDB544ULL,0x8C5AD4926EB17B11ULL,IUnityGraphics)



// Certain Unity APIs (GL.IssuePluginEvent, CommandBuffer.IssuePluginEvent) can callback into native plugins.
// Provide them with an address to a function of this signature.
typedef void (UNITY_INTERFACE_API * UnityRenderingEvent)(int eventId);
typedef void (UNITY_INTERFACE_API * UnityRenderingEventAndData)(int eventId, void* data);
void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityRenderingExtEvent(UnityRenderingExtEventType event, void* data);


enum UnityRenderingExtTextureFormat
{
    kUnityRenderingExtFormatNone = 0, kUnityRenderingExtFormatFirst = kUnityRenderingExtFormatNone,

    // sRGB formats
    kUnityRenderingExtFormatR8_SRGB,
    kUnityRenderingExtFormatR8G8_SRGB,
    kUnityRenderingExtFormatR8G8B8_SRGB,
    kUnityRenderingExtFormatR8G8B8A8_SRGB,

    // 8 bit integer formats
    kUnityRenderingExtFormatR8_UNorm,
    kUnityRenderingExtFormatR8G8_UNorm,
    kUnityRenderingExtFormatR8G8B8_UNorm,
    kUnityRenderingExtFormatR8G8B8A8_UNorm,
    kUnityRenderingExtFormatR8_SNorm,
    kUnityRenderingExtFormatR8G8_SNorm,
    kUnityRenderingExtFormatR8G8B8_SNorm,
    kUnityRenderingExtFormatR8G8B8A8_SNorm,
    kUnityRenderingExtFormatR8_UInt,
    kUnityRenderingExtFormatR8G8_UInt,
    kUnityRenderingExtFormatR8G8B8_UInt,
    kUnityRenderingExtFormatR8G8B8A8_UInt,
    kUnityRenderingExtFormatR8_SInt,
    kUnityRenderingExtFormatR8G8_SInt,
    kUnityRenderingExtFormatR8G8B8_SInt,
    kUnityRenderingExtFormatR8G8B8A8_SInt,

    // 16 bit integer formats
    kUnityRenderingExtFormatR16_UNorm,
    kUnityRenderingExtFormatR16G16_UNorm,
    kUnityRenderingExtFormatR16G16B16_UNorm,
    kUnityRenderingExtFormatR16G16B16A16_UNorm,
    kUnityRenderingExtFormatR16_SNorm,
    kUnityRenderingExtFormatR16G16_SNorm,
    kUnityRenderingExtFormatR16G16B16_SNorm,
    kUnityRenderingExtFormatR16G16B16A16_SNorm,
    kUnityRenderingExtFormatR16_UInt,
    kUnityRenderingExtFormatR16G16_UInt,
    kUnityRenderingExtFormatR16G16B16_UInt,
    kUnityRenderingExtFormatR16G16B16A16_UInt,
    kUnityRenderingExtFormatR16_SInt,
    kUnityRenderingExtFormatR16G16_SInt,
    kUnityRenderingExtFormatR16G16B16_SInt,
    kUnityRenderingExtFormatR16G16B16A16_SInt,

    // 32 bit integer formats
    kUnityRenderingExtFormatR32_UInt,
    kUnityRenderingExtFormatR32G32_UInt,
    kUnityRenderingExtFormatR32G32B32_UInt,
    kUnityRenderingExtFormatR32G32B32A32_UInt,
    kUnityRenderingExtFormatR32_SInt,
    kUnityRenderingExtFormatR32G32_SInt,
    kUnityRenderingExtFormatR32G32B32_SInt,
    kUnityRenderingExtFormatR32G32B32A32_SInt,

    // HDR formats
    kUnityRenderingExtFormatR16_SFloat,
    kUnityRenderingExtFormatR16G16_SFloat,
    kUnityRenderingExtFormatR16G16B16_SFloat,
    kUnityRenderingExtFormatR16G16B16A16_SFloat,
    kUnityRenderingExtFormatR32_SFloat,
    kUnityRenderingExtFormatR32G32_SFloat,
    kUnityRenderingExtFormatR32G32B32_SFloat,
    kUnityRenderingExtFormatR32G32B32A32_SFloat,

    // Luminance and Alpha format
    kUnityRenderingExtFormatL8_UNorm,
    kUnityRenderingExtFormatA8_UNorm,
    kUnityRenderingExtFormatA16_UNorm,

    // BGR formats
    kUnityRenderingExtFormatB8G8R8_SRGB,
    kUnityRenderingExtFormatB8G8R8A8_SRGB,
    kUnityRenderingExtFormatB8G8R8_UNorm,
    kUnityRenderingExtFormatB8G8R8A8_UNorm,
    kUnityRenderingExtFormatB8G8R8_SNorm,
    kUnityRenderingExtFormatB8G8R8A8_SNorm,
    kUnityRenderingExtFormatB8G8R8_UInt,
    kUnityRenderingExtFormatB8G8R8A8_UInt,
    kUnityRenderingExtFormatB8G8R8_SInt,
    kUnityRenderingExtFormatB8G8R8A8_SInt,

    // 16 bit packed formats
    kUnityRenderingExtFormatR4G4B4A4_UNormPack16,
    kUnityRenderingExtFormatB4G4R4A4_UNormPack16,
    kUnityRenderingExtFormatR5G6B5_UNormPack16,
    kUnityRenderingExtFormatB5G6R5_UNormPack16,
    kUnityRenderingExtFormatR5G5B5A1_UNormPack16,
    kUnityRenderingExtFormatB5G5R5A1_UNormPack16,
    kUnityRenderingExtFormatA1R5G5B5_UNormPack16,

    // Packed formats
    kUnityRenderingExtFormatE5B9G9R9_UFloatPack32,
    kUnityRenderingExtFormatB10G11R11_UFloatPack32,

    kUnityRenderingExtFormatA2B10G10R10_UNormPack32,
    kUnityRenderingExtFormatA2B10G10R10_UIntPack32,
    kUnityRenderingExtFormatA2B10G10R10_SIntPack32,
    kUnityRenderingExtFormatA2R10G10B10_UNormPack32,
    kUnityRenderingExtFormatA2R10G10B10_UIntPack32,
    kUnityRenderingExtFormatA2R10G10B10_SIntPack32,
    kUnityRenderingExtFormatA2R10G10B10_XRSRGBPack32,
    kUnityRenderingExtFormatA2R10G10B10_XRUNormPack32,
    kUnityRenderingExtFormatR10G10B10_XRSRGBPack32,
    kUnityRenderingExtFormatR10G10B10_XRUNormPack32,
    kUnityRenderingExtFormatA10R10G10B10_XRSRGBPack32,
    kUnityRenderingExtFormatA10R10G10B10_XRUNormPack32,

    // ARGB formats... TextureFormat legacy
    kUnityRenderingExtFormatA8R8G8B8_SRGB,
    kUnityRenderingExtFormatA8R8G8B8_UNorm,
    kUnityRenderingExtFormatA32R32G32B32_SFloat,

    // Depth Stencil for formats
    kUnityRenderingExtFormatD16_UNorm,
    kUnityRenderingExtFormatD24_UNorm,
    kUnityRenderingExtFormatD24_UNorm_S8_UInt,
    kUnityRenderingExtFormatD32_SFloat,
    kUnityRenderingExtFormatD32_SFloat_S8_Uint,
    kUnityRenderingExtFormatS8_Uint,

    // Compression formats
    kUnityRenderingExtFormatRGBA_DXT1_SRGB,
    kUnityRenderingExtFormatRGBA_DXT1_UNorm,
    kUnityRenderingExtFormatRGBA_DXT3_SRGB,
    kUnityRenderingExtFormatRGBA_DXT3_UNorm,
    kUnityRenderingExtFormatRGBA_DXT5_SRGB,
    kUnityRenderingExtFormatRGBA_DXT5_UNorm,
    kUnityRenderingExtFormatR_BC4_UNorm,
    kUnityRenderingExtFormatR_BC4_SNorm,
    kUnityRenderingExtFormatRG_BC5_UNorm,
    kUnityRenderingExtFormatRG_BC5_SNorm,
    kUnityRenderingExtFormatRGB_BC6H_UFloat,
    kUnityRenderingExtFormatRGB_BC6H_SFloat,
    kUnityRenderingExtFormatRGBA_BC7_SRGB,
    kUnityRenderingExtFormatRGBA_BC7_UNorm,

    kUnityRenderingExtFormatRGB_PVRTC_2Bpp_SRGB,
    kUnityRenderingExtFormatRGB_PVRTC_2Bpp_UNorm,
    kUnityRenderingExtFormatRGB_PVRTC_4Bpp_SRGB,
    kUnityRenderingExtFormatRGB_PVRTC_4Bpp_UNorm,
    kUnityRenderingExtFormatRGBA_PVRTC_2Bpp_SRGB,
    kUnityRenderingExtFormatRGBA_PVRTC_2Bpp_UNorm,
    kUnityRenderingExtFormatRGBA_PVRTC_4Bpp_SRGB,
    kUnityRenderingExtFormatRGBA_PVRTC_4Bpp_UNorm,

    kUnityRenderingExtFormatRGB_ETC_UNorm,
    kUnityRenderingExtFormatRGB_ETC2_SRGB,
    kUnityRenderingExtFormatRGB_ETC2_UNorm,
    kUnityRenderingExtFormatRGB_A1_ETC2_SRGB,
    kUnityRenderingExtFormatRGB_A1_ETC2_UNorm,
    kUnityRenderingExtFormatRGBA_ETC2_SRGB,
    kUnityRenderingExtFormatRGBA_ETC2_UNorm,

    kUnityRenderingExtFormatR_EAC_UNorm,
    kUnityRenderingExtFormatR_EAC_SNorm,
    kUnityRenderingExtFormatRG_EAC_UNorm,
    kUnityRenderingExtFormatRG_EAC_SNorm,

    kUnityRenderingExtFormatRGBA_ASTC4X4_SRGB,
    kUnityRenderingExtFormatRGBA_ASTC4X4_UNorm,
    kUnityRenderingExtFormatRGBA_ASTC5X5_SRGB,
    kUnityRenderingExtFormatRGBA_ASTC5X5_UNorm,
    kUnityRenderingExtFormatRGBA_ASTC6X6_SRGB,
    kUnityRenderingExtFormatRGBA_ASTC6X6_UNorm,
    kUnityRenderingExtFormatRGBA_ASTC8X8_SRGB,
    kUnityRenderingExtFormatRGBA_ASTC8X8_UNorm,
    kUnityRenderingExtFormatRGBA_ASTC10X10_SRGB,
    kUnityRenderingExtFormatRGBA_ASTC10X10_UNorm,
    kUnityRenderingExtFormatRGBA_ASTC12X12_SRGB,
    kUnityRenderingExtFormatRGBA_ASTC12X12_UNorm,

    // Video formats
    kUnityRenderingExtFormatYUV2,

    // Automatic formats, back-end decides
    kUnityRenderingExtFormatLDRAuto,
    kUnityRenderingExtFormatHDRAuto,
    kUnityRenderingExtFormatDepthAuto,
    kUnityRenderingExtFormatShadowAuto,
    kUnityRenderingExtFormatVideoAuto, kUnityRenderingExtFormatLast = kUnityRenderingExtFormatVideoAuto, // Remove?
};


struct UnityRenderingExtTextureUpdateParamsV2
{
    void*        texData;                           // source data for the texture update. Must be set by the plugin
    intptr_t     textureID;                         // texture ID of the texture to be updated.
    unsigned int userData;                          // user defined data. Set by the plugin
    UnityRenderingExtTextureFormat format;          // format of the texture to be updated
    unsigned int width;                             // width of the texture
    unsigned int height;                            // height of the texture
    unsigned int bpp;                               // texture bytes per pixel.
};
