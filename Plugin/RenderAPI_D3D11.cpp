#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

// #if SUPPORT_D3D11

#include <assert.h>
#include <tchar.h>
#include <windows.h>
#include <d3d11_1.h>
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
    /* resources shared by VLC */
    ID3D11Device            *d3deviceVLC;
    ID3D11DeviceContext     *d3dctxVLC;
    ID3D11Texture2D         *textureVLC; // shared between VLC and the app
    HANDLE                  sharedHandled; // handle of the texture used by VLC and the app
    ID3D11RenderTargetView  *textureRenderTarget;

    /* Direct3D11 device/context */
    ID3D11Device        *d3device;
    ID3D11DeviceContext *d3dctx;

    UINT vertexBufferStride;
    ID3D11Buffer *pVertexBuffer;

    UINT quadIndexCount;

    /* texture VLC renders into */
    ID3D11Texture2D          *texture;
    ID3D11ShaderResourceView *textureShaderInput;

    CRITICAL_SECTION sizeLock; // the ReportSize callback cannot be called during/after the Cleanup_cb is called
    unsigned width, height;
    void (*ReportSize)(void *ReportOpaque, unsigned width, unsigned height);
    void *ReportOpaque;

    bool updated;
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
	void CreateResources(struct render_context *ctx, ID3D11Device *d3device, ID3D11DeviceContext *d3dctx);
	void ReleaseResources(struct render_context *ctx);
    void DebugInUnity(LPCSTR message);

private:
	ID3D11Device* m_Device;
    render_context Context;
    const UINT Width = SCREEN_WIDTH;
    const UINT Height = SCREEN_HEIGHT;
    bool initialized;
    // const std::mutex text_lock;
};

// d3d11 callbacks
bool UpdateOutput_cb( void *opaque, const libvlc_video_direct3d_cfg_t *cfg, libvlc_video_output_cfg_t *out );
void Swap_cb( void* opaque );
bool StartRendering_cb( void *opaque, bool enter, const libvlc_video_direct3d_hdr10_metadata_t *hdr10 );
bool SelectPlane_cb( void *opaque, size_t plane );
bool Setup_cb( void **opaque, const libvlc_video_direct3d_device_cfg_t *cfg, libvlc_video_direct3d_device_setup_t *out );
void Cleanup_cb( void *opaque );
void Resize_cb( void *opaque,
                    void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                    void *report_opaque );

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
            if(d3d == NULL)
            {
                DEBUG("Could not retrieve IUnityGraphicsD3D11 \n");
                return;
            }
            ID3D11Device* d3device = d3d->GetDevice();
            if(d3device == NULL)
            {
                DEBUG("Could not retrieve d3device \n");
                return;
            }
            ID3D11DeviceContext* d3dctx;
            d3device->GetImmediateContext(&d3dctx);
            if(d3dctx == NULL)
            {
                DEBUG("Could not retrieve d3dctx \n");
                return;
            }
            CreateResources(&Context, d3device, d3dctx);
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

void Update(render_context* ctx, UINT width, UINT height)
{
    DXGI_FORMAT renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    HRESULT hr;
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
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.CPUAccessFlags = 0;
    texDesc.ArraySize = 1;
    texDesc.Format = renderFormat;
    texDesc.Height = height;
    texDesc.Width  = width;
    
    hr = ctx->d3device->CreateTexture2D( &texDesc, NULL, &ctx->texture );
    if (FAILED(hr))
    {
        DEBUG("CreateTexture2D FAILED \n");
    }
    else
    {
        DEBUG("CreateTexture2D SUCCEEDED.\n");
    }

    IDXGIResource1* sharedResource = NULL;

    hr = ctx->texture->QueryInterface(&sharedResource);
    if(FAILED(hr))
    {
        DEBUG("get IDXGIResource1 FAILED \n");
        abort();
    }

    // ctx->texture->QueryInterface(&IID_ID3D11Resource1, (LPVOID*) &sharedResource);
    hr = sharedResource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ, NULL, &ctx->sharedHandled);
    if(FAILED(hr))
    {
        _com_error error(hr);
        DEBUG("sharedResource->CreateSharedHandle FAILED %s \n", error.ErrorMessage());
        abort();
    }

    sharedResource->Release();

    ID3D11Device1* d3d11VLC1;
    hr = ctx->d3deviceVLC->QueryInterface(&d3d11VLC1);
    if(FAILED(hr))
    {
        _com_error error(hr);
        DEBUG("QueryInterface ID3D11Device1 FAILED %s \n", error.ErrorMessage());
        abort();
    }
    
    hr = d3d11VLC1->OpenSharedResource1(ctx->sharedHandled, __uuidof(ID3D11Texture2D), (void**)&ctx->textureVLC);
    if(FAILED(hr))
    {
        _com_error error(hr);
        DEBUG("ctx->d3device->OpenSharedResource FAILED %s \n", error.ErrorMessage());
        abort();
    }

    d3d11VLC1->Release();

    // ID3D11Device1_OpenSharedResource1(m_Device, sys->sharedHandle, &IID_ID3D11Resource, (void**)&copyTexture);

    // et tu fais ta ID3D11ShaderResourceView a partir de copyTexture

    D3D11_SHADER_RESOURCE_VIEW_DESC resviewDesc;
    ZeroMemory(&resviewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    resviewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resviewDesc.Texture2D.MipLevels = 1;
    resviewDesc.Format = texDesc.Format;
    hr = ctx->d3device->CreateShaderResourceView(ctx->texture, &resviewDesc, &ctx->textureShaderInput );
    if (FAILED(hr)) 
    {
        DEBUG("CreateShaderResourceView FAILED \n");
    }
    else
    {
        DEBUG("CreateShaderResourceView SUCCEEDED.\n");
    }

    D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
    ZeroMemory(&renderTargetViewDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
    renderTargetViewDesc.Format = texDesc.Format;
    renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    ID3D11RenderTargetView* renderTarget;
    FLOAT blueRGBA[] = { 0.0f, 0.0f, 1.0f, 1.0f };
    ctx->d3device->CreateRenderTargetView(ctx->texture, &renderTargetViewDesc, &renderTarget);
    ctx->d3dctx->ClearRenderTargetView(renderTarget, blueRGBA);
    renderTarget->Release();

    //*
    hr = ctx->d3deviceVLC->CreateRenderTargetView(ctx->textureVLC, &renderTargetViewDesc, &ctx->textureRenderTarget);
    /*/
    hr = ctx->d3device->CreateRenderTargetView(ctx->texture, &renderTargetViewDesc, &ctx->textureRenderTarget);
    //*/
    if (FAILED(hr))
    {
        DEBUG("CreateRenderTargetView FAILED \n");
    }
}

void RenderAPI_D3D11::CreateResources(struct render_context *ctx, ID3D11Device *d3device, ID3D11DeviceContext *d3dctx)
{
    DEBUG("Entering CreateResources \n");

 	HRESULT hr;

    assert(ctx != nullptr);
    
    ZeroMemory(ctx, sizeof(*ctx));

    InitializeCriticalSection(&ctx->sizeLock);

    ctx->width = Width;
    ctx->height = Height;
    ctx->d3device = d3device;
    ctx->d3dctx = d3dctx;

    UINT creationFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT; /* needed for hardware decoding */
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG; //TODO: remove for release mode

    hr = D3D11CreateDevice(NULL,
                        D3D_DRIVER_TYPE_HARDWARE,
                        NULL,
                        creationFlags,
                        NULL,
                        NULL,
                        D3D11_SDK_VERSION,
                        &ctx->d3deviceVLC,
                        NULL,
                        &ctx->d3dctxVLC);

    if(FAILED(hr))
    {
        DEBUG("FAILED to create d3d11 device and context \n");
        abort();
    }

    DEBUG("Configuring multithread \n");

    /* The ID3D11Device must have multithread protection */
    ID3D10Multithread *pMultithread;
    hr = ctx->d3device->QueryInterface(&pMultithread);
    if (SUCCEEDED(hr)) {
        pMultithread->SetMultithreadProtected(TRUE);
        pMultithread->Release();
    }

    ctx->quadIndexCount = 6;

    Update(ctx, SCREEN_WIDTH, SCREEN_HEIGHT);
    DEBUG("Exiting CreateResources.\n");
}


void RenderAPI_D3D11::ReleaseResources(struct render_context *ctx)
{
    DEBUG("Entering ReleaseResources.\n");
    ctx->d3deviceVLC->Release();
    ctx->d3dctxVLC->Release();

    ctx->textureRenderTarget->Release();
    ctx->textureShaderInput->Release();
    ctx->texture->Release();
    ctx->pVertexBuffer->Release();
    ctx->d3dctx->Release();
    ctx->d3device->Release();
}

bool UpdateOutput_cb( void *opaque, const libvlc_video_direct3d_cfg_t *cfg, libvlc_video_output_cfg_t *out )
{
    DEBUG("Entering UpdateOutput_cb.\n");
    struct render_context *ctx = static_cast<struct render_context *>( opaque );

    assert(ctx != nullptr);

    DXGI_FORMAT renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    Update(ctx, cfg->width, cfg->height);
    DEBUG("Done \n");

    out->surface_format = renderFormat;
    out->full_range     = true;
    out->colorspace     = libvlc_video_colorspace_BT709;
    out->primaries      = libvlc_video_primaries_BT709;
    out->transfer       = libvlc_video_transfer_func_SRGB;

    DEBUG("Exiting UpdateOutput_cb \n");

    return true;
}

void Swap_cb( void* opaque )
{
    DEBUG("libvlc SWAP \n");

    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    ctx->updated = true;
}

bool StartRendering_cb( void *opaque, bool enter, const libvlc_video_direct3d_hdr10_metadata_t *hdr10 )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    if ( enter )
    {
        DEBUG("StartRendering enter ");
        static const FLOAT blackRGBA[4] = {0.5f, 0.5f, 0.0f, 1.0f};

        /* force unbinding the input texture, otherwise we get:
         * OMSetRenderTargets: Resource being set to OM RenderTarget slot 0 is still bound on input! */
        // ID3D11ShaderResourceView *reset = NULL;
        // ctx->d3dctx->PSSetShaderResources(0, 1, &reset);
        //ctx->d3dctx->Flush();

        ctx->d3dctxVLC->ClearRenderTargetView( ctx->textureRenderTarget, blackRGBA);
        DEBUG("out \n");
        return true;
    }

    DEBUG("StartRendering end ");
    /* render into the swapchain */
    /*static const FLOAT orangeRGBA[4] = {1.0f, 0.5f, 0.0f, 1.0f};

    ctx->d3dctx->OMSetRenderTargets(1, &ctx->swapchainRenderTarget, NULL);
    ctx->d3dctx->ClearRenderTargetView(ctx->swapchainRenderTarget, orangeRGBA);

    ctx->d3dctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->d3dctx->IASetInputLayout(ctx->pShadersInputLayout);
    UINT offset = 0;
    ctx->d3dctx->IASetVertexBuffers(0, 1, &ctx->pVertexBuffer, &ctx->vertexBufferStride, &offset);
    ctx->d3dctx->IASetIndexBuffer(ctx->pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);

    ctx->d3dctx->VSSetShader(ctx->pVS, 0, 0);

    ctx->d3dctx->PSSetSamplers(0, 1, &ctx->samplerState);

    ctx->d3dctx->PSSetShaderResources(0, 1, &ctx->textureShaderInput);

    ctx->d3dctx->PSSetShader(ctx->pPS, 0, 0);

    D3D11_VIEWPORT viewport = { };
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = SCREEN_WIDTH;
    viewport.Height = SCREEN_HEIGHT;

    ctx->d3dctx->RSSetViewports(1, &viewport);

    ctx->d3dctx->DrawIndexed(ctx->quadIndexCount, 0, 0);*/
    DEBUG("out \n");
    return true;
}

bool SelectPlane_cb( void *opaque, size_t plane )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    if ( plane != 0 ) // we only support one packed RGBA plane (DXGI_FORMAT_R8G8B8A8_UNORM)
        return false;
    ctx->d3dctxVLC->OMSetRenderTargets( 1, &ctx->textureRenderTarget, NULL );
    return true;
}

bool Setup_cb( void **opaque, const libvlc_video_direct3d_device_cfg_t *cfg, libvlc_video_direct3d_device_setup_t *out )
{
    struct render_context *ctx = static_cast<struct render_context *>(*opaque);
    out->device_context = ctx->d3dctxVLC;
    return true;
}

void Cleanup_cb( void *opaque )
{
    // here we can release all things Direct3D11 for good (if playing only one file)
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
}

void Resize_cb( void *opaque,
                       void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                       void *report_opaque )
{
    DEBUG("Resize_cb called \n");

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
    *out_updated = true;
    //Context.d3dctx->OMSetRenderTargets(1, &Context.textureRenderTarget, NULL);
    static const FLOAT orangeRGBA[4] = {1.0f, 0.5f, 0.0f, 1.0f};
    //*
    Context.d3dctxVLC->OMSetRenderTargets(1, &Context.textureRenderTarget, NULL);
    Context.d3dctxVLC->ClearRenderTargetView(Context.textureRenderTarget, orangeRGBA);
    /*/
    Context.d3dctx->ClearRenderTargetView(Context.textureRenderTarget, orangeRGBA);
    //*/
    return Context.textureShaderInput;
    /*
    *out_updated = Context.updated;
    Context.updated = false;
    return (void*)Context.textureShaderInput;*/

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
