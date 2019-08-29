#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

#if SUPPORT_D3D11

#include <assert.h>
#include <tchar.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "Unity/IUnityGraphicsD3D11.h"
#include "Log.h"

#include <dxgi1_2.h>
#include <comdef.h>

#define SCREEN_WIDTH  1500
#define SCREEN_HEIGHT  900
#define BORDER_LEFT    (-0.95f)
#define BORDER_RIGHT   ( 0.85f)
#define BORDER_TOP     ( 0.95f)
#define BORDER_BOTTOM  (-0.90f)


static const char *shaderStr = "\
Texture2D shaderTexture;\n\
SamplerState samplerState;\n\
struct PS_INPUT\n\
{\n\
    float4 position     : SV_POSITION;\n\
    float4 textureCoord : TEXCOORD0;\n\
};\n\
\n\
float4 PShader(PS_INPUT In) : SV_TARGET\n\
{\n\
    return shaderTexture.Sample(samplerState, In.textureCoord);\n\
}\n\
\n\
struct VS_INPUT\n\
{\n\
    float4 position     : POSITION;\n\
    float4 textureCoord : TEXCOORD0;\n\
};\n\
\n\
struct VS_OUTPUT\n\
{\n\
    float4 position     : SV_POSITION;\n\
    float4 textureCoord : TEXCOORD0;\n\
};\n\
\n\
VS_OUTPUT VShader(VS_INPUT In)\n\
{\n\
    return In;\n\
}\n\
";

struct SHADER_INPUT {
    struct {
        FLOAT x;
        FLOAT y;
        FLOAT z;
    } position;
    struct {
        FLOAT x;
        FLOAT y;
    } texture;
};


struct render_context
{
    /* Direct3D11 device/context */
    ID3D11Device        *d3device;
    ID3D11DeviceContext *d3dctx;

    IDXGISwapChain         *swapchain;
    ID3D11RenderTargetView *swapchainRenderTarget;

    /* our vertex/pixel shader */
    ID3D11VertexShader *pVS;
    ID3D11PixelShader  *pPS;
    ID3D11InputLayout  *pShadersInputLayout;

    UINT vertexBufferStride;
    ID3D11Buffer *pVertexBuffer;

    UINT quadIndexCount;
    ID3D11Buffer *pIndexBuffer;

    ID3D11SamplerState *samplerState;

    /* texture VLC renders into */
    ID3D11Texture2D          *texture;
    ID3D11ShaderResourceView *textureShaderInput;
    ID3D11RenderTargetView   *textureRenderTarget;

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
    void SetupTextureInfo(UINT width, UINT height, void* hwnd);
	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);
    void* getVideoFrame(bool* out_updated) override;

private:
	void CreateResources(struct render_context *ctx);
	void ReleaseResources(struct render_context *ctx);
    void DebugInUnity(LPCSTR message);
    // d3d11 callbacks
    static bool UpdateOutput_cb( void *opaque, const libvlc_video_direct3d_cfg_t *cfg, libvlc_video_output_cfg_t *out );
    static void Swap_cb( void* opaque );
    static void EndRender(struct render_context *ctx);
    static bool StartRendering_cb( void *opaque, bool enter, const libvlc_video_direct3d_hdr10_metadata_t *hdr10 );
    static bool SelectPlane_cb( void *opaque, size_t plane );
    static bool Setup_cb( void **opaque, const libvlc_video_direct3d_device_cfg_t *cfg, libvlc_video_direct3d_device_setup_t *out );
    static void Cleanup_cb( void *opaque );
    static void Resize_cb( void *opaque,
                        void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                        void *report_opaque );

private:
	ID3D11Device* m_Device;
    render_context Context;
    const UINT Width = 1024;
    const UINT Height = 768;
    // HWND Hwnd;
    void* Hwnd;
    bool initialized;
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

void RenderAPI_D3D11::SetupTextureInfo(UINT width, UINT height, void* hwnd)
{
    DEBUG("Entering SetupTextureInfo");

    // Width = width;
    // Height = height;
    Hwnd = hwnd;
}

void RenderAPI_D3D11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    DEBUG("Entering ProcessDeviceEvent \n");

	switch (type)
	{
        case kUnityGfxDeviceEventInitialize:
        {
            IUnityGraphicsD3D11* d3d = interfaces->Get<IUnityGraphicsD3D11>();
            // m_Device = d3d->GetDevice();
            // struct render_context Context = { 0 };
            // Context.d3device = d3d->GetDevice();
            CreateResources(&Context);
            break;
        }
        case kUnityGfxDeviceEventShutdown:
        {
      //      ReleaseResources(&Context);
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
    DXGI_SWAP_CHAIN_DESC scd = { 0 };

    if(Hwnd == nullptr)
    {
        DEBUG("ERROR: Hwnd == nullptr is true \n");
        DEBUG("Exiting CreateResources \n");
        return;
    } 
    else{
        DEBUG("Hwnd == nullptr is false \n");
        DEBUG("Continuing... \n");
    }
    if(Hwnd == NULL){
        DEBUG("ERROR: Hwnd == NULL is true \n");
        DEBUG("Exiting CreateResources \n");
        return;
    }
    else{
        DEBUG("Hwnd == NULL is false \n");
        DEBUG("Continuing... \n");
    }

    // RECT rect;
    // int width;
    // int height;

    // if(GetWindowRect(Hwnd, &rect))
    // {
    //     width = rect.right - rect.left;
    //     height = rect.bottom - rect.top;
    // }

    // Width = width;
    // Height = height;

    // Width = 1024;
    // Height = 768;
    
    ctx->width = Width;
    ctx->height = Height;

    DEBUG("WIDTH: %u \n", Width);
    DEBUG("HEIGHT: %u \n", Height);
    DEBUG("Hwnd: %u \n", Hwnd);

    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferDesc.Width = Width;
    scd.BufferDesc.Height = Height;
    scd.OutputWindow = (HWND)Hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    
    UINT creationFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT; /* needed for hardware decoding */
// #ifndef NDEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
// #endif


 hr = D3D11CreateDeviceAndSwapChain(NULL,
                                  D3D_DRIVER_TYPE_HARDWARE,
                                  NULL,
                                  creationFlags,
                                  NULL,
                                  NULL,
                                  D3D11_SDK_VERSION,
                                  &scd,
                                  &ctx->swapchain,
                                  &ctx->d3device,
                                  NULL,
                                  &ctx->d3dctx);

if(FAILED(hr)){
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();
        DEBUG("FAILED to D3D11CreateDeviceAndSwapChain \n");
        DEBUG("HR => %s \n", errMsg);
    }
    else{
        DEBUG("CREATE D3D11CreateDeviceAndSwapChain SUCCEEDED \n");
    }

    /* The ID3D11Device must have multithread protection */
    ID3D10Multithread *pMultithread;
    hr = ctx->d3device->QueryInterface( __uuidof(ID3D10Multithread), (void **)&pMultithread);
    if (SUCCEEDED(hr)) {
        pMultithread->SetMultithreadProtected(TRUE);
        pMultithread->Release();
    }

    ID3D11Texture2D *pBackBuffer;
    hr = ctx->swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer); // this fails!
    if(FAILED(hr))
    {
        DEBUG("Failed to get buffer from swapchain.\n");
        return;
    }

    ctx->d3device->CreateRenderTargetView(pBackBuffer, NULL, &ctx->swapchainRenderTarget);
    pBackBuffer->Release();

    DEBUG("Compiling shaders....\n");

    ID3D10Blob *VS, *PS, *pErrBlob;
    char *err;
    hr = D3DCompile(shaderStr, strlen(shaderStr),
                    NULL, NULL, NULL, "VShader", "vs_4_0", 0, 0, &VS, &pErrBlob);
    err = pErrBlob ? (char*)pErrBlob->GetBufferPointer() : NULL;
    hr = D3DCompile(shaderStr, strlen(shaderStr),
                    NULL, NULL, NULL, "PShader", "ps_4_0", 0, 0, &PS, &pErrBlob);
    err = pErrBlob ? (char*)pErrBlob->GetBufferPointer() : NULL;

    DEBUG("Creating vertex and pixel shaders...\n");
    ctx->d3device->CreateVertexShader(VS->GetBufferPointer(), VS->GetBufferSize(), NULL, &ctx->pVS);
    ctx->d3device->CreatePixelShader(PS->GetBufferPointer(), PS->GetBufferSize(), NULL, &ctx->pPS);

    D3D11_INPUT_ELEMENT_DESC ied[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = ctx->d3device->CreateInputLayout(ied, 2, VS->GetBufferPointer(), VS->GetBufferSize(), &ctx->pShadersInputLayout);
    if(FAILED(hr))
    {
        DEBUG("Failed to CreateInputLayout.\n");
        return;
    }

    SHADER_INPUT OurVertices[] =
    {
        {BORDER_LEFT,  BORDER_BOTTOM, 0.0f,  0.0f, 1.0f},
        {BORDER_RIGHT, BORDER_BOTTOM, 0.0f,  1.0f, 1.0f},
        {BORDER_RIGHT, BORDER_TOP,    0.0f,  1.0f, 0.0f},
        {BORDER_LEFT,  BORDER_TOP,    0.0f,  0.0f, 0.0f},
    };

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));

    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(OurVertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    DEBUG("Creating buffer from device");

    ctx->d3device->CreateBuffer(&bd, NULL, &ctx->pVertexBuffer);
    ctx->vertexBufferStride = sizeof(OurVertices[0]);

    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->d3dctx->Map(ctx->pVertexBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &ms);
    memcpy(ms.pData, OurVertices, sizeof(OurVertices));
    ctx->d3dctx->Unmap(ctx->pVertexBuffer, NULL);

    ctx->quadIndexCount = 6;
    D3D11_BUFFER_DESC quadDesc = { 0 };
    quadDesc.Usage = D3D11_USAGE_DYNAMIC;
    quadDesc.ByteWidth = sizeof(WORD) * ctx->quadIndexCount;
    quadDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    quadDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ctx->d3device->CreateBuffer(&quadDesc, NULL, &ctx->pIndexBuffer);

    ctx->d3dctx->Map(ctx->pIndexBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &ms);
    WORD *triangle_pos = static_cast<WORD*>(ms.pData);
    triangle_pos[0] = 3;
    triangle_pos[1] = 1;
    triangle_pos[2] = 0;

    triangle_pos[3] = 2;
    triangle_pos[4] = 1;
    triangle_pos[5] = 3;
    ctx->d3dctx->Unmap(ctx->pIndexBuffer, NULL);

    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory(&sampDesc, sizeof(sampDesc));
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = ctx->d3device->CreateSamplerState(&sampDesc, &ctx->samplerState);
    if(FAILED(hr))
    {
        DEBUG("Failed to CreateSamplerState.\n");
    }

    DEBUG("Exiting CreateResources.\n");
}


void RenderAPI_D3D11::ReleaseResources(struct render_context *ctx)
{
    DEBUG("Entering ReleaseResources.\n");

	ctx->samplerState->Release();
    ctx->textureRenderTarget->Release();
    ctx->textureShaderInput->Release();
    ctx->texture->Release();
    ctx->pShadersInputLayout->Release();
    ctx->pVS->Release();
    ctx->pPS->Release();
    ctx->pIndexBuffer->Release();
    ctx->pVertexBuffer->Release();
    ctx->swapchain->Release();
    ctx->swapchainRenderTarget->Release();
    ctx->d3dctx->Release();
    ctx->d3device->Release();
}


bool RenderAPI_D3D11::UpdateOutput_cb( void *opaque, const libvlc_video_direct3d_cfg_t *cfg, libvlc_video_output_cfg_t *out )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );

    HRESULT hr;

    DXGI_FORMAT renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

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

    hr = ctx->d3device->CreateTexture2D( &texDesc, NULL, &ctx->texture );
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC resviewDesc;
    ZeroMemory(&resviewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    resviewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resviewDesc.Texture2D.MipLevels = 1;
    resviewDesc.Format = texDesc.Format;
    hr = ctx->d3device->CreateShaderResourceView(ctx->texture, &resviewDesc, &ctx->textureShaderInput );
    if (FAILED(hr)) return false;

    D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {
        .Format = texDesc.Format,
        .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
    };
    hr = ctx->d3device->CreateRenderTargetView(ctx->texture, &renderTargetViewDesc, &ctx->textureRenderTarget);
    if (FAILED(hr)) return false;


    out->surface_format = renderFormat;
    out->full_range     = true;
    out->colorspace     = libvlc_video_colorspace_BT709;
    out->primaries      = libvlc_video_primaries_BT709;
    out->transfer       = libvlc_video_transfer_func_SRGB;

    return true;
}

void RenderAPI_D3D11::Swap_cb( void* opaque )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    ctx->swapchain->Present( 0, 0 );
    ctx->updated = true;
}

void RenderAPI_D3D11::EndRender(struct render_context *ctx)
{
    /* render into the swapchain */
    static const FLOAT orangeRGBA[4] = {1.0f, 0.5f, 0.0f, 1.0f};

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

    DXGI_SWAP_CHAIN_DESC scd;
    ctx->swapchain->GetDesc(&scd);
    RECT currentRect;
    GetWindowRect(scd.OutputWindow, &currentRect);

    D3D11_VIEWPORT viewport = { 0 };
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    // todo: fixme
    viewport.Width = 1024; //currentRect.right - currentRect.left;
    viewport.Height = 768; //currentRect.bottom - currentRect.top;

    ctx->d3dctx->RSSetViewports(1, &viewport);

    ctx->d3dctx->DrawIndexed(ctx->quadIndexCount, 0, 0);
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

    EndRender( ctx );
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
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
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

    //return nullptr;

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



#endif // #if SUPPORT_D3D11
