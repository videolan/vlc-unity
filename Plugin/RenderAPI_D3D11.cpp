#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

// #if SUPPORT_D3D11


#include <assert.h>
#include <tchar.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "Unity/IUnityGraphicsD3D11.h"
#include "Log.h"

#include <algorithm>
#include <dxgi1_2.h>
#include <comdef.h>
#include <mingw.mutex.h>

#define SCREEN_WIDTH  100
#define SCREEN_HEIGHT  100
#define BORDER_LEFT    (-0.95f)
#define BORDER_RIGHT   ( 0.85f)
#define BORDER_TOP     ( 0.95f)
#define BORDER_BOTTOM  (-0.90f)

struct render_context
{
    /* Direct3D11 device/context */
    ID3D11Device        *d3device;
    ID3D11DeviceContext *d3dctx;

    UINT vertexBufferStride;
    ID3D11Buffer *pVertexBuffer;

    UINT quadIndexCount;


    /* texture VLC renders into */
    ID3D11Texture2D          *texture;
    ID3D11ShaderResourceView *textureShaderInput;
    ID3D11RenderTargetView   *textureRenderTarget;

    ID3D11ShaderResourceView *textureShaderInput2;

    CRITICAL_SECTION sizeLock; // the ReportSize callback cannot be called during/after the Cleanup_cb is called
    unsigned width, height;
    void (*ReportSize)(void *ReportOpaque, unsigned width, unsigned height);
    void *ReportOpaque;

    bool updated = false;
};

class RenderAPI_D3D11 : public RenderAPI
{
public:
	RenderAPI_D3D11();
	virtual ~RenderAPI_D3D11() { }
    virtual void setVlcContext(libvlc_media_player_t *mp) override;
	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);
    void* getVideoFrame(bool* out_updated) override;

private:
	void CreateResources(struct render_context *ctx);
	void ReleaseResources(struct render_context *ctx);
    void DebugInUnity(LPCSTR message);
    // d3d11 callbacks
    static bool UpdateOutput_cb( void *opaque, const libvlc_video_direct3d_cfg_t *cfg, libvlc_video_output_cfg_t *out );
    static void Swap_cb( void* opaque );
    static bool StartRendering_cb( void *opaque, bool enter, const libvlc_video_direct3d_hdr10_metadata_t *hdr10 );
    static bool SelectPlane_cb( void *opaque, size_t plane );
    static bool Setup_cb( void **opaque, const libvlc_video_direct3d_device_cfg_t *cfg, libvlc_video_direct3d_device_setup_t *out );
    static void Cleanup_cb( void *opaque );
    static void Resize_cb( void *opaque,
                        void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                        void *report_opaque );

private:
	ID3D11Device* m_Device;
    ID3D11DeviceContext* m_Context;
    render_context Context;
    const UINT Width = SCREEN_WIDTH;
    const UINT Height = SCREEN_HEIGHT;
    bool initialized;
    // const std::mutex text_lock;
};


RenderAPI* CreateRenderAPI_D3D11()
{
	return new RenderAPI_D3D11();
}

RenderAPI_D3D11::RenderAPI_D3D11()
	: m_Device(NULL)
{
}

void RenderAPI_D3D11::setVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[D3D11] setVlcContext %p", this);

    libvlc_video_direct3d_set_callbacks( mp, libvlc_video_direct3d_engine_d3d11,
                                    Setup_cb, Cleanup_cb, Resize_cb, UpdateOutput_cb,
                                    Swap_cb, StartRendering_cb, SelectPlane_cb,
                                    &Context );
}

void RenderAPI_D3D11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    DEBUG("Entering ProcessDeviceEvent \n");

	switch (type)
	{
        case kUnityGfxDeviceEventInitialize:
        {
            IUnityGraphicsD3D11* d3d = interfaces->Get<IUnityGraphicsD3D11>();
            // if(d3d == NULL)
            // {
            //     DEBUG("Could not retrieve IUnityGraphicsD3D11 \n");
            //     return;
            // }
            // m_Device = d3d->GetDevice();
            // if(m_Device == NULL)
            // {
            //     DEBUG("Could not retrieve m_Device \n");
            //     return;
            // }
            // m_Device->GetImmediateContext(&m_Context);
            // if(m_Context == NULL)
            // {
            //     DEBUG("Could not retrieve m_Context \n");
            //     return;
            // }
            CreateResources(&Context);
            break;
        }
        case kUnityGfxDeviceEventShutdown:
        {
            ReleaseResources(&Context);
            break;
        }
        case kUnityGfxDeviceEventAfterReset:
        {
            break;
        }
        case kUnityGfxDeviceEventBeforeReset:
        {
            break;
        }
    }
}

void RenderAPI_D3D11::CreateResources(struct render_context *ctx)
{
    DEBUG("Entering CreateResources \n");

 	HRESULT hr;

    assert(ctx != nullptr);
    
    ZeroMemory(ctx, sizeof(*ctx));

    InitializeCriticalSection(&ctx->sizeLock);

    ctx->width = Width;
    ctx->height = Height;

    UINT creationFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT; /* needed for hardware decoding */
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG; //TODO: remove for release mode

    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(NULL,
                        D3D_DRIVER_TYPE_HARDWARE,
                        NULL,
                        creationFlags,
                        NULL,
                        NULL,
                        D3D11_SDK_VERSION,
                        &ctx->d3device,
                        NULL,
                        &ctx->d3dctx);

    if(FAILED(hr))
    {
        DEBUG("FAILED to create d3d11 device and context \n");
        abort();
    }

    /* The ID3D11Device must have multithread protection */
    ID3D10Multithread *pMultithread;
    hr = ctx->d3device->QueryInterface( __uuidof(ID3D10Multithread), (void **)&pMultithread);
    if (SUCCEEDED(hr)) {
        pMultithread->SetMultithreadProtected(TRUE);
        pMultithread->Release();
    }

    DEBUG("Creating buffer from device");

    ctx->quadIndexCount = 6;

    DEBUG("Exiting CreateResources.\n");
}


void RenderAPI_D3D11::ReleaseResources(struct render_context *ctx)
{
    DEBUG("Entering ReleaseResources.\n");

    ctx->textureRenderTarget->Release();
    ctx->textureShaderInput->Release();
    ctx->texture->Release();
    ctx->pVertexBuffer->Release();
    ctx->d3dctx->Release();
    ctx->d3device->Release();
}


bool RenderAPI_D3D11::UpdateOutput_cb( void *opaque, const libvlc_video_direct3d_cfg_t *cfg, libvlc_video_output_cfg_t *out )
{
    DEBUG("Entering UpdateOutput_cb.\n");
    struct render_context *ctx = static_cast<struct render_context *>( opaque );

    assert(ctx != nullptr);

    HRESULT hr;

    DXGI_FORMAT renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    DEBUG("start releasing d3d objects.\n");

    if (ctx->texture)
    {
        ctx->texture->Release();
        ctx->texture = NULL;
    }
    if (ctx->textureShaderInput)
    {
        ctx->textureShaderInput->Release();
        ctx->textureShaderInput = NULL;
    }
    if (ctx->textureRenderTarget)
    {
        ctx->textureRenderTarget->Release();
        ctx->textureRenderTarget = NULL;
    }

    DEBUG("Done releasing d3d objects.\n");

    /* interim texture */
    D3D11_TEXTURE2D_DESC texDesc = { 0 };
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.MiscFlags = 0;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.CPUAccessFlags = 0;
    texDesc.ArraySize = 1;
    texDesc.Format = renderFormat;
    texDesc.Height = cfg->height;
    texDesc.Width  = cfg->width;

    DEBUG("Done setting up texDesc.\n");

    if(ctx->d3device == nullptr)
    {
        DEBUG("d3device is NULL \n");
    }

    hr = ctx->d3device->CreateTexture2D( &texDesc, NULL, &ctx->texture );
    if (FAILED(hr))
    {
        DEBUG("CreateTexture2D FAILED \n");
        return false;
    }
    else
    {
        DEBUG("CreateTexture2D SUCCEEDED.\n");
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC resviewDesc;
    ZeroMemory(&resviewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    resviewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resviewDesc.Texture2D.MipLevels = 1;
    resviewDesc.Format = texDesc.Format;
    hr = ctx->d3device->CreateShaderResourceView(ctx->texture, &resviewDesc, &ctx->textureShaderInput );
    if (FAILED(hr)) 
    {
        DEBUG("CreateShaderResourceView FAILED \n");
        return false;
    }
    else
    {
        DEBUG("CreateShaderResourceView SUCCEEDED.\n");
    }

    D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
    ZeroMemory(&renderTargetViewDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
    renderTargetViewDesc.Format = texDesc.Format,
    renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
    
    hr = ctx->d3device->CreateRenderTargetView(ctx->texture, &renderTargetViewDesc, &ctx->textureRenderTarget);
    if (FAILED(hr))
    {
        DEBUG("CreateRenderTargetView FAILED \n");
        return false;
    }

    out->surface_format = renderFormat;
    out->full_range     = true;
    out->colorspace     = libvlc_video_colorspace_BT709;
    out->primaries      = libvlc_video_primaries_BT709;
    out->transfer       = libvlc_video_transfer_func_SRGB;

    DEBUG("Exiting UpdateOutput_cb \n");

    return true;
}

void RenderAPI_D3D11::Swap_cb( void* opaque )
{
    DEBUG("libvlc SWAP \n");

    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    ctx->updated = true;
}

bool RenderAPI_D3D11::StartRendering_cb( void *opaque, bool enter, const libvlc_video_direct3d_hdr10_metadata_t *hdr10 )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    if ( enter )
    {
        static const FLOAT blackRGBA[4] = {0.5f, 0.5f, 0.0f, 1.0f};

        /* force unbinding the input texture, otherwise we get:
         * OMSetRenderTargets: Resource being set to OM RenderTarget slot 0 is still bound on input! */
        ID3D11ShaderResourceView *reset = NULL;
        ctx->d3dctx->PSSetShaderResources(0, 1, &reset);
        //ctx->d3dctx->Flush();

        ctx->d3dctx->ClearRenderTargetView( ctx->textureRenderTarget, blackRGBA);
        return true;
    }

    return true;
}

bool RenderAPI_D3D11::SelectPlane_cb( void *opaque, size_t plane )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    if ( plane != 0 ) // we only support one packed RGBA plane (DXGI_FORMAT_R8G8B8A8_UNORM)
        return false;
    ctx->d3dctx->OMSetRenderTargets( 1, &ctx->textureRenderTarget, NULL );
    return true;
}

bool RenderAPI_D3D11::Setup_cb( void **opaque, const libvlc_video_direct3d_device_cfg_t *cfg, libvlc_video_direct3d_device_setup_t *out )
{
    struct render_context *ctx = static_cast<struct render_context *>(*opaque);
    out->device_context = ctx->d3dctx;
    return true;
}

void RenderAPI_D3D11::Cleanup_cb( void *opaque )
{
    // here we can release all things Direct3D11 for good (if playing only one file)
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
}

void RenderAPI_D3D11::Resize_cb( void *opaque,
                       void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                       void *report_opaque )
{
    DEBUG("Resize_cb called \n");
    DEBUG("YOLO \n");

    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    DEBUG("ctx ptr => %p \n", ctx);
    EnterCriticalSection(&ctx->sizeLock);
    ctx->ReportSize = report_size_change;
    ctx->ReportOpaque = report_opaque;

    if (ctx->ReportSize != nullptr)
    {
        DEBUG("Invoking ctx->ReportSize(ctx->ReportOpaque, ctx->width, ctx->height) with width=%u and height=%u \n", ctx->width, ctx->height);

        /* report our initial size */
        ctx->ReportSize(ctx->ReportOpaque, ctx->width, ctx->height);
    }
    LeaveCriticalSection(&ctx->sizeLock);

    DEBUG("Exiting Resize_cb");
}

void* RenderAPI_D3D11::getVideoFrame(bool* out_updated)
{
    DEBUG("Entering getVideoFrame \n");

    return (void*)Context.textureShaderInput;

    // std::lock_guard<std::mutex> lock(text_lock);
    // if (out_updated)
    //     *out_updated = updated;
    // if (updated)
    // {
    //     std::swap(idx_swap, idx_display);
    //     updated = false;
    // }
    // //DEBUG("get Video Frame %u", tex[idx_display]);
    // return (void*)(size_t)tex[idx_display];
}



// #endif // #if SUPPORT_D3D11
