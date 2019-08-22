#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

#if SUPPORT_D3D11

#include <assert.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "Unity/IUnityGraphicsD3D11.h"


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
};

class RenderAPI_D3D11 : public RenderAPI
{
public:
	RenderAPI_D3D11();
	virtual ~RenderAPI_D3D11() { }

	virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces);

private:
	void CreateResources(struct render_context *ctx);
	void ReleaseResources();

private:
	ID3D11Device* m_Device;
	// ID3D11Buffer* m_VB; // vertex buffer
	// ID3D11Buffer* m_CB; // constant buffer
	// ID3D11VertexShader* m_VertexShader;
	// ID3D11PixelShader* m_PixelShader;
	// ID3D11InputLayout* m_InputLayout;
	// ID3D11RasterizerState* m_RasterState;
	// ID3D11BlendState* m_BlendState;
	// ID3D11DepthStencilState* m_DepthState;
};


RenderAPI* CreateRenderAPI_D3D11()
{
	return new RenderAPI_D3D11();
}

RenderAPI_D3D11::RenderAPI_D3D11()
	: m_Device(NULL)
	// , m_VB(NULL)
	// , m_CB(NULL)
	// , m_VertexShader(NULL)
	// , m_PixelShader(NULL)
	// , m_InputLayout(NULL)
	// , m_RasterState(NULL)
	// , m_BlendState(NULL)
	// , m_DepthState(NULL)
{
}

void RenderAPI_D3D11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	switch (type)
	{
	case kUnityGfxDeviceEventInitialize:
	{
		IUnityGraphicsD3D11* d3d = interfaces->Get<IUnityGraphicsD3D11>();
		m_Device = d3d->GetDevice();
		struct render_context Context = { 0 };
		CreateResources(&Context);
		break;
	}
	case kUnityGfxDeviceEventShutdown:
		ReleaseResources();
		break;
	}
}


void RenderAPI_D3D11::CreateResources(struct render_context *ctx)
{
 	HRESULT hr;
    DXGI_SWAP_CHAIN_DESC scd = { 0 };

    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = SCREEN_WIDTH;
    scd.BufferDesc.Height = SCREEN_HEIGHT;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    UINT creationFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT; /* needed for hardware decoding */
#ifndef NDEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    /* The ID3D11Device must have multithread protection */
    ID3D10Multithread *pMultithread;
    hr = ctx->d3device->QueryInterface( __uuidof(ID3D10Multithread), (void **)&pMultithread);
    if (SUCCEEDED(hr)) {
        pMultithread->SetMultithreadProtected(TRUE);
        pMultithread->Release();
    }

    ID3D11Texture2D *pBackBuffer;
    ctx->swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);

    ctx->d3device->CreateRenderTargetView(pBackBuffer, NULL, &ctx->swapchainRenderTarget);
    pBackBuffer->Release();

    ID3D10Blob *VS, *PS, *pErrBlob;
    char *err;
    hr = D3DCompile(shaderStr, strlen(shaderStr),
                    NULL, NULL, NULL, "VShader", "vs_4_0", 0, 0, &VS, &pErrBlob);
    err = pErrBlob ? (char*)pErrBlob->GetBufferPointer() : NULL;
    hr = D3DCompile(shaderStr, strlen(shaderStr),
                    NULL, NULL, NULL, "PShader", "ps_4_0", 0, 0, &PS, &pErrBlob);
    err = pErrBlob ? (char*)pErrBlob->GetBufferPointer() : NULL;

    ctx->d3device->CreateVertexShader(VS->GetBufferPointer(), VS->GetBufferSize(), NULL, &ctx->pVS);
    ctx->d3device->CreatePixelShader(PS->GetBufferPointer(), PS->GetBufferSize(), NULL, &ctx->pPS);

    D3D11_INPUT_ELEMENT_DESC ied[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = ctx->d3device->CreateInputLayout(ied, 2, VS->GetBufferPointer(), VS->GetBufferSize(), &ctx->pShadersInputLayout);
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
}


void RenderAPI_D3D11::ReleaseResources()
{
	// SAFE_RELEASE(m_VB);
	// SAFE_RELEASE(m_CB);
	// SAFE_RELEASE(m_VertexShader);
	// SAFE_RELEASE(m_PixelShader);
	// SAFE_RELEASE(m_InputLayout);
	// SAFE_RELEASE(m_RasterState);
	// SAFE_RELEASE(m_BlendState);
	// SAFE_RELEASE(m_DepthState);
}


static bool UpdateOutput_cb( void *opaque, const libvlc_video_direct3d_cfg_t *cfg, libvlc_video_output_cfg_t *out )
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

static void Swap_cb( void* opaque )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    ctx->swapchain->Present( 0, 0 );
}

static void EndRender(struct render_context *ctx)
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
    viewport.Width = SCREEN_WIDTH; //currentRect.right - currentRect.left;
    viewport.Height = SCREEN_HEIGHT; //currentRect.bottom - currentRect.top;

    ctx->d3dctx->RSSetViewports(1, &viewport);

    ctx->d3dctx->DrawIndexed(ctx->quadIndexCount, 0, 0);
}

static bool StartRendering_cb( void *opaque, bool enter, const libvlc_video_direct3d_hdr10_metadata_t *hdr10 )
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

static bool SelectPlane_cb( void *opaque, size_t plane )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    if ( plane != 0 ) // we only support one packed RGBA plane (DXGI_FORMAT_R8G8B8A8_UNORM)
        return false;
    ctx->d3dctx->OMSetRenderTargets( 1, &ctx->textureRenderTarget, NULL );
    return true;
}

static bool Setup_cb( void **opaque, const libvlc_video_direct3d_device_cfg_t *cfg, libvlc_video_direct3d_device_setup_t *out )
{
    struct render_context *ctx = static_cast<struct render_context *>(*opaque);
    out->device_context = ctx->d3dctx;
    return true;
}

static void Cleanup_cb( void *opaque )
{
    // here we can release all things Direct3D11 for good (if playing only one file)
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
}

static void Resize_cb( void *opaque,
                       void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                       void *report_opaque )
{
    struct render_context *ctx = static_cast<struct render_context *>( opaque );
    EnterCriticalSection(&ctx->sizeLock);
    ctx->ReportSize = report_size_change;
    ctx->ReportOpaque = report_opaque;

    if (ctx->ReportSize != nullptr)
    {
        /* report our initial size */
        ctx->ReportSize(ctx->ReportOpaque, ctx->width, ctx->height);
    }
    LeaveCriticalSection(&ctx->sizeLock);
}

#endif // #if SUPPORT_D3D11
