#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

// #if SUPPORT_D3D11

#include <assert.h>
#include <tchar.h>
#include <windows.h>
#include <d3d11_1.h>
#if SUPPORT_D3D12
#include <d3d12.h>
#endif
#include <d3dcompiler.h>
#if defined(SHOW_WATERMARK)
#include <d2d1.h>
#include <dwrite.h>
#include <chrono>
#endif // SHOW_WATERMARK
#include "Unity/IUnityGraphicsD3D11.h"
#if SUPPORT_D3D12
#include "Unity/IUnityGraphicsD3D12.h"
#endif
#include "Log.h"

#include <algorithm>
#include <dxgi1_2.h>

#include <memory>
#include <random>

#if defined(SHOW_WATERMARK)
extern "C" bool libvlc_unity_trial_tick();
extern "C" uint32_t libvlc_unity_trial_seconds_remaining();
extern "C" bool libvlc_unity_trial_is_paused();
extern "C" bool libvlc_unity_trial_is_stopped();

static int64_t getCurrentTimeMs()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}
#endif

#if !defined(UWP)
// #include <comdef.h> // enable for debugging
#endif

#define SCREEN_WIDTH  100
#define SCREEN_HEIGHT  100

// Watermark position enum
enum class WatermarkPosition
{
    BOTTOM_CENTER = 0,
    CENTER = 1,
    TOP_CENTER = 2
};

class ReadWriteTexture
{
public:
    HANDLE                   m_sharedHandle         = nullptr; // handle of the texture used by VLC and the app
    ID3D11RenderTargetView   *m_textureRenderTarget = nullptr;
    unsigned                 width                  = 0;       // Texture width
    unsigned                 height                 = 0;       // Texture height
#if defined(SHOW_WATERMARK)
    ID2D1RenderTarget        *d2dRenderTarget       = nullptr; // Per-texture D2D render target
    ID2D1SolidColorBrush     *textBrush             = nullptr; // Per-texture text brush
#endif // SHOW_WATERMARK
    int                      rwt_bit_depth          = false;

    void Cleanup();
    void Update(unsigned width, unsigned height, IUnknown *m_d3deviceUnity, ID3D11Device *m_d3deviceVLC
#if defined(SHOW_WATERMARK)
                , ID2D1Factory *d2dFactory, IDWriteFactory *dwriteFactory, IDWriteTextFormat *textFormat
#endif // SHOW_WATERMARK
                );
    void *GetUnityTexture();

private:
    bool Update11(unsigned width, unsigned height, DXGI_FORMAT renderFormat, ID3D11Device *m_d3deviceUnity);
#if SUPPORT_D3D12
    bool Update12(unsigned width, unsigned height, DXGI_FORMAT renderFormat, ID3D12Device *m_d3deviceUnity);
#endif

    ID3D11Texture2D          *m_textureUnity11       = nullptr;
    ID3D11ShaderResourceView *m_textureShaderInput11 = nullptr;
#if SUPPORT_D3D12
    bool                      is_d3d12               = false;
    ID3D12Resource           *m_textureUnity12       = nullptr;
#endif
};

class RenderAPI_D3D11 : public RenderAPI
{
public:
    RenderAPI_D3D11(UnityGfxRenderer apiType);
    ~RenderAPI_D3D11();

    virtual void setVlcContext(libvlc_media_player_t *mp) override;
    virtual void unsetVlcContext(libvlc_media_player_t *mp) override;
    virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
    void* getVideoFrame(unsigned width, unsigned height, bool* out_updated) override;
    void setColorSpace(int color_space) override;
    void setbitDepthFormat(int bit_depth) override;

    /* VLC callbacks */
    bool UpdateOutput(const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *out);
    void Swap();
    bool MakeCurrent(bool enter);
    bool SelectPlane(size_t plane, void *output);
    bool Setup(const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out);
    void Report(
                libvlc_video_output_resize_cb report_size_change,
                libvlc_video_output_mouse_move_cb report_mouse_move,
                libvlc_video_output_mouse_press_cb report_mouse_press,
                libvlc_video_output_mouse_release_cb report_mouse_release,
                void *report_opaque);

    std::unique_ptr<ReadWriteTexture> read_write[2];
    bool                    write_on_first = false;
    ReadWriteTexture        *current_texture = nullptr;
    ReadWriteTexture* m_textureForUnity = nullptr;
private:
    void CreateResources();
    void ReleaseResources();
    void Update(UINT width, UINT height);

    UnityGfxRenderer         m_unityRendererType    = kUnityGfxRendererNull;

    /* Unity side resources */
    IUnknown                *m_d3deviceUnity        = nullptr;

    /* VLC side resources */
    ID3D11Device            *m_d3deviceVLC          = nullptr;
    ID3D11DeviceContext     *m_d3dctxVLC            = nullptr;

#if defined(SHOW_WATERMARK)
    /* Videolabs watermark */
    ID2D1Factory            *d2dFactory             = nullptr;
    IDWriteFactory          *dwriteFactory          = nullptr;
    IDWriteTextFormat       *textFormat             = nullptr;
    IDWriteTextFormat       *countdownTextFormat    = nullptr; // Smaller font for countdown
    float fontSize = 180.0f;
    float countdownFontSize = 48.0f;
    WatermarkPosition       m_watermarkPosition     = WatermarkPosition::BOTTOM_CENTER;
    int64_t                 m_lastWatermarkMoveMs   = 0;
    static const int64_t    WATERMARK_REPOSITION_INTERVAL_MS = 2500;
#endif // SHOW_WATERMARK

    CRITICAL_SECTION m_sizeLock; // the ReportSize callback cannot be called during/after the Cleanup_cb is called
    unsigned m_width = 0;
    unsigned m_height = 0;
    libvlc_video_output_resize_cb m_ReportSize = nullptr;
    void *m_reportOpaque = nullptr;
    const FLOAT blackRGBA[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool m_updated = false;

    CRITICAL_SECTION m_outputLock; // the ReportSize callback cannot be called during/after the Cleanup_cb is called

    bool m_linear = false;
    int m_bit_depth = 8;

    libvlc_media_player_t *m_mp = nullptr;

#if defined(SHOW_WATERMARK)
    bool m_trialMessageDrawn = false;
    ID3D11Texture2D* m_trialTexture = nullptr;
    ID3D11ShaderResourceView* m_trialTextureView = nullptr;
    ID2D1RenderTarget* m_trialD2DTarget = nullptr;
    ID2D1SolidColorBrush* m_trialBrush = nullptr;
    unsigned m_trialTextureWidth = 0;
    unsigned m_trialTextureHeight = 0;

    bool ensureTrialTexture(unsigned width, unsigned height);
    void releaseTrialTexture();
#endif
};

// VLC C-style callbacks
bool UpdateOutput_cb(void *opaque, const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *out)
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    return me->UpdateOutput(cfg, out);
}

void Swap_cb(void* opaque)
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    me->Swap();
}

bool MakeCurrent_cb(void *opaque, bool enter)
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    return me->MakeCurrent(enter);
}

bool SelectPlane_cb(void *opaque, size_t plane, void *output)
{
    return ((RenderAPI_D3D11*)opaque)->SelectPlane(plane, output);
}

bool Setup_cb(void **opaque, const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out)
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(*opaque);
    return me->Setup(cfg, out);
}

void Cleanup_cb(void *opaque)
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    me->read_write[0]->Cleanup();
    me->read_write[1]->Cleanup();
}

void Report_cb(void *opaque,
               libvlc_video_output_resize_cb report_size_change,
               libvlc_video_output_mouse_move_cb report_mouse_move,
               libvlc_video_output_mouse_press_cb report_mouse_press,
               libvlc_video_output_mouse_release_cb report_mouse_release,
               void *report_opaque)
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    me->Report(report_size_change, report_mouse_move, report_mouse_press, report_mouse_release, report_opaque);
}

RenderAPI* CreateRenderAPI_D3D11(UnityGfxRenderer apiType)
{
    return new RenderAPI_D3D11(apiType);
}

RenderAPI_D3D11::RenderAPI_D3D11(UnityGfxRenderer apiType)
    : m_unityRendererType(apiType)
{
    ZeroMemory(&m_sizeLock, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(&m_sizeLock);
    ZeroMemory(&m_outputLock, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(&m_outputLock);

    read_write[0].reset(new ReadWriteTexture());
    read_write[1].reset(new ReadWriteTexture());
    current_texture = read_write[0].get();
    m_textureForUnity = nullptr;
    read_write[0]->rwt_bit_depth = m_bit_depth;
    read_write[1]->rwt_bit_depth = m_bit_depth;
}

RenderAPI_D3D11::~RenderAPI_D3D11()
{
    DeleteCriticalSection(&m_sizeLock);
    DeleteCriticalSection(&m_outputLock);
}

void RenderAPI_D3D11::setVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[D3D11] Subscribing output callbacks %p \n", this);

    m_mp = mp;

    CreateResources();

    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_d3d11,
                                      Setup_cb, Cleanup_cb, Report_cb, UpdateOutput_cb,
                                      Swap_cb, MakeCurrent_cb, nullptr, nullptr, SelectPlane_cb,
                                      this);
}

void RenderAPI_D3D11::unsetVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[D3D11] Unsubscribing to output callbacks %p \n", this);

    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_disable,
                                      Setup_cb, Cleanup_cb, Report_cb, UpdateOutput_cb,
                                      Swap_cb, MakeCurrent_cb, nullptr, nullptr, SelectPlane_cb,
                                      this);

    ReleaseResources();
}

void RenderAPI_D3D11::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    DEBUG("Entering ProcessDeviceEvent \n");

    switch (type)
    {
        case kUnityGfxDeviceEventInitialize:
        {
            m_d3deviceUnity = NULL;
#if SUPPORT_D3D12
            if (m_unityRendererType == kUnityGfxRendererD3D12)
            {
                IUnityGraphicsD3D12v2* d3d12v2 = interfaces->Get<IUnityGraphicsD3D12v2>();
                if (d3d12v2 != NULL)
                {
                    m_d3deviceUnity = (IUnknown*)d3d12v2->GetDevice();
                }
                if (m_d3deviceUnity == NULL)
                {
                    IUnityGraphicsD3D12* d3d12 = interfaces->Get<IUnityGraphicsD3D12>();
                    if (d3d12 != NULL)
                    {
                        m_d3deviceUnity = (IUnknown*)d3d12->GetDevice();
                    }
                }
                if (m_d3deviceUnity != NULL)
                {
                    DEBUG("Using Direct3D 12\n");
                }
            }
#endif
            if (m_d3deviceUnity == NULL)
            {
                IUnityGraphicsD3D11* d3d11 = interfaces->Get<IUnityGraphicsD3D11>();
                if (d3d11 != NULL)
                    m_d3deviceUnity = (IUnknown*)d3d11->GetDevice();
                if (m_d3deviceUnity != NULL)
                {
#if SUPPORT_D3D12
                    if (m_unityRendererType == kUnityGfxRendererD3D12)
                        DEBUG("Using Direct3D 11 (fallback)\n");
                    else
#endif
                        DEBUG("Using Direct3D 11\n");
                }
            }
            if (m_d3deviceUnity == NULL)
            {
                DEBUG("Could not retrieve d3device\n");
                return;
            }
            break;
        }
        case kUnityGfxDeviceEventShutdown:
        {
            ReleaseResources();
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

void RenderAPI_D3D11::Update(UINT width, UINT height)
{
    EnterCriticalSection(&m_outputLock);

    m_width = width;
    m_height = height;

#if defined(SHOW_WATERMARK)
    read_write[0]->Update(m_width, m_height, m_d3deviceUnity, m_d3deviceVLC, d2dFactory, dwriteFactory, textFormat);
    read_write[1]->Update(m_width, m_height, m_d3deviceUnity, m_d3deviceVLC, d2dFactory, dwriteFactory, textFormat);
#else
    read_write[0]->Update(m_width, m_height, m_d3deviceUnity, m_d3deviceVLC);
    read_write[1]->Update(m_width, m_height, m_d3deviceUnity, m_d3deviceVLC);
#endif // SHOW_WATERMARK

    LeaveCriticalSection(&m_outputLock);
}

bool ReadWriteTexture::Update11(unsigned width, unsigned height, DXGI_FORMAT renderFormat, ID3D11Device *m_d3deviceUnity)
{
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.CPUAccessFlags = 0;
    texDesc.ArraySize = 1;
    texDesc.Format = renderFormat;
    texDesc.Height = height;
    texDesc.Width = width;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    HRESULT hr;
    hr = m_d3deviceUnity->CreateTexture2D(&texDesc, NULL, &m_textureUnity11);
    if (FAILED(hr))
    {
        DEBUG("CreateTexture2D FAILED \n");
        return false;
    }
    DEBUG("CreateTexture2D SUCCEEDED.\n");

    IDXGIResource1* sharedResource = NULL;
    hr = m_textureUnity11->QueryInterface(&sharedResource);
    if (FAILED(hr))
    {
        DEBUG("get IDXGIResource1 FAILED \n");
        m_textureUnity11->Release();
        m_textureUnity11 = nullptr;
        return false;
    }

    hr = sharedResource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &m_sharedHandle);
    if (FAILED(hr))
    {
        DEBUG("sharedResource->CreateSharedHandle FAILED \n");
        sharedResource->Release();
        m_textureUnity11->Release();
        m_textureUnity11 = nullptr;
        return false;
    }
    sharedResource->Release();
    sharedResource = NULL;

    D3D11_SHADER_RESOURCE_VIEW_DESC resviewDesc;
    ZeroMemory(&resviewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    resviewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resviewDesc.Texture2D.MipLevels = 1;
    resviewDesc.Format = texDesc.Format;
    hr = m_d3deviceUnity->CreateShaderResourceView(m_textureUnity11, &resviewDesc, &m_textureShaderInput11);
    if (FAILED(hr))
    {
        DEBUG("CreateShaderResourceView FAILED \n");
        CloseHandle(m_sharedHandle);
        m_sharedHandle = nullptr;
        m_textureUnity11->Release();
        m_textureUnity11 = nullptr;
        return false;
    }
    DEBUG("CreateShaderResourceView SUCCEEDED.\n");

    return true;
}

#if SUPPORT_D3D12
bool ReadWriteTexture::Update12(unsigned width, unsigned height, DXGI_FORMAT renderFormat, ID3D12Device *m_d3deviceUnity)
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = renderFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = renderFormat;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    HRESULT hr;
    hr = m_d3deviceUnity->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_SHARED, &texDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&m_textureUnity12));
    if (FAILED(hr)) {
        DEBUG("CreateCommittedResource FAILED \n");
        return false;
    }
    DEBUG("CreateCommittedResource SUCCEEDED.\n");

    hr = m_d3deviceUnity->CreateSharedHandle(m_textureUnity12, NULL, GENERIC_ALL, NULL, &m_sharedHandle);
    if (FAILED(hr))
    {
        DEBUG("CreateSharedHandle FAILED \n");
        m_textureUnity12->Release();
        m_textureUnity12 = nullptr;
        return false;
    }

    return true;
}
#endif

void ReadWriteTexture::Update(unsigned width, unsigned height, IUnknown *m_d3deviceUnity, ID3D11Device *m_d3deviceVLC
#if defined(SHOW_WATERMARK)
                              , ID2D1Factory *d2dFactory, IDWriteFactory *dwriteFactory, IDWriteTextFormat *textFormat
#endif // SHOW_WATERMARK
                             )
{
#if defined(SHOW_WATERMARK)
    (void)dwriteFactory;
#endif
    Cleanup();

    DXGI_FORMAT renderFormat;
    if (rwt_bit_depth == 16) {
        renderFormat = DXGI_FORMAT_R16G16B16A16_UNORM;
    } else {
        renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    DEBUG("Done releasing d3d objects.\n");

    this->width = width;
    this->height = height;

    HRESULT hr;
    bool succeeded = false;
#if SUPPORT_D3D12
    is_d3d12 = false;
    ID3D12Device *m_d3deviceUnity12 = nullptr;
    hr = m_d3deviceUnity->QueryInterface(&m_d3deviceUnity12);
    if (SUCCEEDED(hr))
    {
        is_d3d12 = true;
        succeeded = Update12(width, height, renderFormat, m_d3deviceUnity12);
        m_d3deviceUnity12->Release();
    }
    else
#endif
    {
        ID3D11Device *m_d3deviceUnity11 = nullptr;
        hr = m_d3deviceUnity->QueryInterface(&m_d3deviceUnity11);
        if (SUCCEEDED(hr))
        {
            succeeded = Update11(width, height, renderFormat, m_d3deviceUnity11);
            m_d3deviceUnity11->Release();
        }
    }
    if (!succeeded)
        return;

    ID3D11Texture2D* textureVLC = nullptr;
    if (m_d3deviceVLC)
    {
        ID3D11Device1* d3d11VLC1;
        hr = m_d3deviceVLC->QueryInterface(&d3d11VLC1);
        if (SUCCEEDED(hr))
        {
            hr = d3d11VLC1->OpenSharedResource1(m_sharedHandle, __uuidof(ID3D11Texture2D), (void**)&textureVLC);
            if (FAILED(hr))
            {
                DEBUG("ctx->d3device->OpenSharedResource FAILED \n");
                textureVLC = nullptr;
            }
            d3d11VLC1->Release();
        }
        else
        {
            DEBUG("QueryInterface ID3D11Device1 FAILED \n");
        }
    }

    if (textureVLC && m_d3deviceVLC)
    {
        D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
        ZeroMemory(&renderTargetViewDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
        renderTargetViewDesc.Format = renderFormat;
        renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        hr = m_d3deviceVLC->CreateRenderTargetView(textureVLC, &renderTargetViewDesc, &m_textureRenderTarget);
        if (FAILED(hr))
        {
            DEBUG("CreateRenderTargetView FAILED \n");
            Cleanup();
        }
    } else {
         DEBUG("Skipping RTV creation as textureVLC or m_d3deviceVLC is invalid.\n");
    }


#if defined(SHOW_WATERMARK)
    if (d2dFactory && textFormat && textureVLC && m_textureRenderTarget)
    {
        IDXGISurface *dxgiSurface = nullptr;
        hr = textureVLC->QueryInterface(__uuidof(IDXGISurface), (void**)&dxgiSurface);
        if (SUCCEEDED(hr) && dxgiSurface)
        {
            D2D1_RENDER_TARGET_PROPERTIES renderTargetProps = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );

            hr = d2dFactory->CreateDxgiSurfaceRenderTarget(dxgiSurface, renderTargetProps, &d2dRenderTarget);
            if (FAILED(hr))
            {
                DEBUG("CreateDxgiSurfaceRenderTarget FAILED \n");
            }
            else
            {
                float r_color = 255.0f / 255.0f;
                float g_color = 102.0f / 255.0f;
                float b_color = 0.0f / 255.0f;
                float a_color = 1.0f;
                hr = d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(r_color, g_color, b_color, a_color), &textBrush);
                if (FAILED(hr))
                {
                    DEBUG("CreateSolidColorBrush FAILED \n");
                    d2dRenderTarget->Release();
                    d2dRenderTarget = nullptr;
                }
            }
            dxgiSurface->Release();
        }
        else
        {
            DEBUG("textureVLC->QueryInterface IDXGISurface FAILED or dxgiSurface is null\n");
        }
    }
#endif // SHOW_WATERMARK

    if (textureVLC) {
        textureVLC->Release();
        textureVLC = NULL;
    }
}

void *ReadWriteTexture::GetUnityTexture()
{
#if SUPPORT_D3D12
    return is_d3d12 ? (void*)m_textureUnity12 : (void*)m_textureShaderInput11;
#else
    return (void*)m_textureShaderInput11;
#endif
}


void RenderAPI_D3D11::CreateResources()
{
    DEBUG("Entering CreateResources \n");

    if (m_d3deviceUnity == NULL)
    {
        DEBUG("Could not retrieve unity d3device %p, aborting... \n", m_d3deviceUnity);
        return;
    }

    HRESULT hr;

    UINT creationFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifndef NDEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    creationFlags |= D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    hr = D3D11CreateDevice(NULL,
                           D3D_DRIVER_TYPE_HARDWARE,
                           NULL,
                           creationFlags,
                           NULL,
                           0,
                           D3D11_SDK_VERSION,
                           &m_d3deviceVLC,
                           NULL,
                           &m_d3dctxVLC);
    DEBUG("CreateResources m_d3dctxVLC = %p this = %p \n", m_d3dctxVLC, this);

    if (FAILED(hr))
    {
        DEBUG("FAILED to create d3d11 device and context \n");
        return;
    }

    DEBUG("Configuring multithread \n");

    ID3D10Multithread *pMultithread;
    hr = m_d3dctxVLC->QueryInterface(&pMultithread);
    if (SUCCEEDED(hr)) {
        pMultithread->SetMultithreadProtected(TRUE);
        pMultithread->Release();
    } else {
        DEBUG("FAILED to SetMultithreadProtected \n");
    }

#if defined(SHOW_WATERMARK)
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &d2dFactory);
    if (FAILED(hr)) {
        DEBUG("FAILED to create D2D1 factory \n");
    }

    if (SUCCEEDED(hr))
    {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dwriteFactory);
        if (FAILED(hr)) {
            if (d2dFactory) d2dFactory->Release(); // Check if d2dFactory was successfully created
            d2dFactory = nullptr;
            DEBUG("FAILED to create DWrite factory \n");
        }
    }


    if (SUCCEEDED(hr) && dwriteFactory)
    {
        hr = dwriteFactory->CreateTextFormat(
            L"Arial",
            nullptr,
            DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"en-us",
            &textFormat
        );
        if (FAILED(hr)) {
            if(dwriteFactory) dwriteFactory->Release();
            dwriteFactory = nullptr;
            if (d2dFactory) d2dFactory->Release();
            d2dFactory = nullptr;
            DEBUG("FAILED to create text format \n");
        }
    }

    // Create countdown text format (smaller font)
    if (SUCCEEDED(hr) && dwriteFactory)
    {
        hr = dwriteFactory->CreateTextFormat(
            L"Arial",
            nullptr,
            DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            countdownFontSize,
            L"en-us",
            &countdownTextFormat
        );
        if (FAILED(hr)) {
            DEBUG("FAILED to create countdown text format \n");
            // Non-fatal - countdown just won't show
        }
    }
#endif // SHOW_WATERMARK

    DEBUG("Exiting CreateResources.\n");
}

void ReadWriteTexture::Cleanup()
{
    DEBUG("Entering Cleanup.\n");

#if defined(SHOW_WATERMARK)
    if (textBrush) {
        textBrush->Release();
        textBrush = nullptr;
    }
    if (d2dRenderTarget) {
        d2dRenderTarget->Release();
        d2dRenderTarget = nullptr;
    }
#endif // SHOW_WATERMARK

    if (m_textureRenderTarget) {
        m_textureRenderTarget->Release();
        m_textureRenderTarget = nullptr;
    }
#if SUPPORT_D3D12
    if (m_textureUnity12) {
        m_textureUnity12->Release();
        m_textureUnity12 = nullptr;
    }
#endif
    if (m_textureShaderInput11) {
        m_textureShaderInput11->Release();
        m_textureShaderInput11 = nullptr;
    }
    if (m_textureUnity11) {
        m_textureUnity11->Release();
        m_textureUnity11 = nullptr;
    }
    if (m_sharedHandle) {
        CloseHandle(m_sharedHandle);
        m_sharedHandle = nullptr;
    }
}

void RenderAPI_D3D11::ReleaseResources()
{
    DEBUG("ReleaseResources called \n");

#if defined(SHOW_WATERMARK)
    releaseTrialTexture();

    if (countdownTextFormat) {
        countdownTextFormat->Release();
        countdownTextFormat = nullptr;
    }
    if (textFormat) {
        textFormat->Release();
        textFormat = nullptr;
    }
    if (dwriteFactory) {
        dwriteFactory->Release();
        dwriteFactory = nullptr;
    }
    if (d2dFactory) {
        d2dFactory->Release();
        d2dFactory = nullptr;
    }
#endif // SHOW_WATERMARK

    if (m_d3deviceVLC) {
        m_d3deviceVLC->Release();
        m_d3deviceVLC = nullptr;
    }

    if (m_d3dctxVLC) {
        m_d3dctxVLC->Release();
        m_d3dctxVLC = nullptr;
    }
}

bool RenderAPI_D3D11::UpdateOutput(const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *out)
{
    DEBUG("Entering UpdateOutput_cb.\n");

    DXGI_FORMAT renderFormat;
    if(m_bit_depth == 16)
    {
        renderFormat = DXGI_FORMAT_R16G16B16A16_UNORM;
    }
    else
    {
        renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    this->Update(cfg->width, cfg->height);

    out->dxgi_format = renderFormat;
    out->full_range     = true;
    out->colorspace     = libvlc_video_colorspace_BT709;
    out->primaries      = libvlc_video_primaries_BT709;
    out->transfer       = m_linear ? libvlc_video_transfer_func_LINEAR : libvlc_video_transfer_func_SRGB;
    out->orientation    = libvlc_video_orient_top_right;

    DEBUG("Exiting UpdateOutput_cb \n");

    return true;
}
void RenderAPI_D3D11::Swap()
{
    EnterCriticalSection(&m_outputLock);

    ReadWriteTexture* textureJustWrittenByVLC = current_texture;

#if defined(SHOW_WATERMARK)
    bool isPaused = libvlc_unity_trial_is_paused();
    bool isStopped = libvlc_unity_trial_is_stopped();
    bool isPlaying = !isPaused && !isStopped;
    bool trialExpired = false;

    DEBUG_VERBOSE("[D3D11] Swap: isPaused=%d, isStopped=%d, isPlaying=%d", isPaused, isStopped, isPlaying);

    if (isPaused && m_textureForUnity != nullptr)
    {
        DEBUG_VERBOSE("[D3D11] Swap: early return (paused)");
        LeaveCriticalSection(&m_outputLock);
        return;
    }

    if (isPlaying)
    {
        trialExpired = !libvlc_unity_trial_tick();
        DEBUG_VERBOSE("[D3D11] Swap: isPlaying=true, trialExpired=%d", trialExpired);
        if (trialExpired && m_mp)
        {
            DEBUG("[D3D11] Swap: calling stop_async due to trial expiry");
            libvlc_media_player_stop_async(m_mp);
        }

        if (!trialExpired)
        {
            int64_t nowMs = getCurrentTimeMs();
            if (nowMs - m_lastWatermarkMoveMs >= WATERMARK_REPOSITION_INTERVAL_MS)
            {
                m_lastWatermarkMoveMs = nowMs;
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, 2);
                m_watermarkPosition = static_cast<WatermarkPosition>(dis(gen));
            }
        }
    }

    bool showTrialMessage = isStopped || trialExpired;
    DEBUG_VERBOSE("[D3D11] Swap: showTrialMessage=%d (isStopped=%d, trialExpired=%d)", showTrialMessage, isStopped, trialExpired);

    DEBUG_VERBOSE("[D3D11] Swap: texture=%p, d2dRT=%p, textFormat=%p, textBrush=%p",
        textureJustWrittenByVLC,
        textureJustWrittenByVLC ? textureJustWrittenByVLC->d2dRenderTarget : nullptr,
        textFormat,
        textureJustWrittenByVLC ? textureJustWrittenByVLC->textBrush : nullptr);

    if (textureJustWrittenByVLC && textureJustWrittenByVLC->d2dRenderTarget && textFormat && textureJustWrittenByVLC->textBrush) {
        if (showTrialMessage)
        {
            DEBUG_VERBOSE("[D3D11] Swap: drawing trial message (black screen)");
            textureJustWrittenByVLC->d2dRenderTarget->BeginDraw();

            D2D1_MATRIX_3X2_F originalTransform;
            textureJustWrittenByVLC->d2dRenderTarget->GetTransform(&originalTransform);

            textureJustWrittenByVLC->d2dRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

            const WCHAR line1[] = L"Trial session expired";
            const WCHAR line2[] = L"Purchase the pro version at https://videolabs.io/store/unity";
            UINT32 line1Length = ARRAYSIZE(line1) - 1;
            UINT32 line2Length = ARRAYSIZE(line2) - 1;

            float maxWidth = textureJustWrittenByVLC->width * 0.9f;
            float line1FontSize = maxWidth / (line1Length * 0.55f);
            float line2FontSize = maxWidth / (line2Length * 0.55f);
            if (line1FontSize > 72.0f) line1FontSize = 72.0f;
            if (line2FontSize > 36.0f) line2FontSize = 36.0f;
            float lineSpacing = line1FontSize * 0.5f;

            float totalHeight = line1FontSize + lineSpacing + line2FontSize;
            float startY = (textureJustWrittenByVLC->height - totalHeight) / 2.0f;

            D2D1_RECT_F line1Rect = D2D1::RectF(
                0,
                startY,
                (float)textureJustWrittenByVLC->width,
                startY + line1FontSize * 1.2f
            );

            D2D1_RECT_F line2Rect = D2D1::RectF(
                0,
                startY + line1FontSize + lineSpacing,
                (float)textureJustWrittenByVLC->width,
                startY + line1FontSize + lineSpacing + line2FontSize * 1.2f
            );

            float centerX = textureJustWrittenByVLC->width / 2.0f;
            D2D1_MATRIX_3X2_F mirror =
                D2D1::Matrix3x2F::Translation(-centerX, 0.0f) *
                D2D1::Matrix3x2F::Scale(-1.0f, 1.0f, D2D1::Point2F(0.0f, 0.0f)) *
                D2D1::Matrix3x2F::Translation(centerX, 0.0f);
            textureJustWrittenByVLC->d2dRenderTarget->SetTransform(mirror * originalTransform);

            IDWriteTextFormat* line1Format = nullptr;
            IDWriteTextFormat* line2Format = nullptr;
            if (dwriteFactory)
            {
                dwriteFactory->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, line1FontSize, L"en-us", &line1Format);
                dwriteFactory->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, line2FontSize, L"en-us", &line2Format);
                if (line1Format) line1Format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                if (line2Format) line2Format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            }

            textureJustWrittenByVLC->d2dRenderTarget->DrawText(
                line1, line1Length,
                line1Format ? line1Format : textFormat,
                line1Rect,
                textureJustWrittenByVLC->textBrush
            );

            textureJustWrittenByVLC->d2dRenderTarget->DrawText(
                line2, line2Length,
                line2Format ? line2Format : countdownTextFormat,
                line2Rect,
                textureJustWrittenByVLC->textBrush
            );

            if (line1Format) line1Format->Release();
            if (line2Format) line2Format->Release();

            textureJustWrittenByVLC->d2dRenderTarget->SetTransform(originalTransform);
            textureJustWrittenByVLC->d2dRenderTarget->EndDraw();
        }
        else if (isPlaying)
        {
            DEBUG_VERBOSE("[D3D11] Swap: drawing watermark (playing)");
            textureJustWrittenByVLC->d2dRenderTarget->BeginDraw();

            D2D1_MATRIX_3X2_F originalTransform;
            textureJustWrittenByVLC->d2dRenderTarget->GetTransform(&originalTransform);

            float padding = 10.0f;
            const WCHAR watermarkText[] = L"Videolabs.io";
            UINT32 textLength = ARRAYSIZE(watermarkText) - 1;
            float approxTextWidth = textLength * (fontSize * 0.5f);

            D2D1_RECT_F textRect;

            switch (m_watermarkPosition) {
                case WatermarkPosition::BOTTOM_CENTER:
                {
                    float textRectLeft = (textureJustWrittenByVLC->width - approxTextWidth) / 2.0f;
                    float textRectTop = textureJustWrittenByVLC->height - fontSize - padding;
                    float textRectRight = textRectLeft + approxTextWidth;
                    float textRectBottom = textureJustWrittenByVLC->height - padding;
                    textRect = D2D1::RectF(textRectLeft, textRectTop, textRectRight, textRectBottom);
                    break;
                }
                case WatermarkPosition::CENTER:
                {
                    float textRectLeft = (textureJustWrittenByVLC->width - approxTextWidth) / 2.0f;
                    float textRectTop = (textureJustWrittenByVLC->height - fontSize) / 2.0f;
                    float textRectRight = textRectLeft + approxTextWidth;
                    float textRectBottom = textRectTop + fontSize;
                    textRect = D2D1::RectF(textRectLeft, textRectTop, textRectRight, textRectBottom);
                    break;
                }
                case WatermarkPosition::TOP_CENTER:
                {
                    float textRectLeft = (textureJustWrittenByVLC->width - approxTextWidth) / 2.0f;
                    float textRectTop = padding;
                    float textRectRight = textRectLeft + approxTextWidth;
                    float textRectBottom = padding + fontSize;
                    textRect = D2D1::RectF(textRectLeft, textRectTop, textRectRight, textRectBottom);
                    break;
                }
                default:
                {
                    float textRectLeft = (textureJustWrittenByVLC->width - approxTextWidth) / 2.0f;
                    float textRectTop = textureJustWrittenByVLC->height - fontSize - padding;
                    float textRectRight = textRectLeft + approxTextWidth;
                    float textRectBottom = textureJustWrittenByVLC->height - padding;
                    textRect = D2D1::RectF(textRectLeft, textRectTop, textRectRight, textRectBottom);
                    break;
                }
            }

            float centerX = textRect.left + (textRect.right - textRect.left) / 2.0f;
            D2D1_MATRIX_3X2_F mirrorTransform =
                D2D1::Matrix3x2F::Translation(-centerX, 0.0f) *
                D2D1::Matrix3x2F::Scale(-1.0f, 1.0f, D2D1::Point2F(0.0f, 0.0f)) *
                D2D1::Matrix3x2F::Translation(centerX, 0.0f);
            textureJustWrittenByVLC->d2dRenderTarget->SetTransform(mirrorTransform * originalTransform);

            textureJustWrittenByVLC->d2dRenderTarget->DrawText(
                watermarkText,
                textLength,
                textFormat,
                textRect,
                textureJustWrittenByVLC->textBrush
            );

            textureJustWrittenByVLC->d2dRenderTarget->SetTransform(originalTransform);

            if (countdownTextFormat)
            {
                uint32_t secondsRemaining = libvlc_unity_trial_seconds_remaining();
                WCHAR countdownText[32];
                swprintf_s(countdownText, L"Trial: %us", secondsRemaining);
                UINT32 countdownLength = (UINT32)wcslen(countdownText);

                float countdownWidth = countdownLength * (countdownFontSize * 0.5f);
                float countdownRectLeft = textureJustWrittenByVLC->width - countdownWidth - padding;
                float countdownRectTop = padding;
                float countdownRectRight = textureJustWrittenByVLC->width - padding;
                float countdownRectBottom = padding + countdownFontSize;
                D2D1_RECT_F countdownRect = D2D1::RectF(countdownRectLeft, countdownRectTop, countdownRectRight, countdownRectBottom);

                float countdownCenterX = countdownRect.left + (countdownRect.right - countdownRect.left) / 2.0f;
                D2D1_MATRIX_3X2_F countdownMirrorTransform =
                    D2D1::Matrix3x2F::Translation(-countdownCenterX, 0.0f) *
                    D2D1::Matrix3x2F::Scale(-1.0f, 1.0f, D2D1::Point2F(0.0f, 0.0f)) *
                    D2D1::Matrix3x2F::Translation(countdownCenterX, 0.0f);
                textureJustWrittenByVLC->d2dRenderTarget->SetTransform(countdownMirrorTransform * originalTransform);

                textureJustWrittenByVLC->d2dRenderTarget->DrawText(
                    countdownText,
                    countdownLength,
                    countdownTextFormat,
                    countdownRect,
                    textureJustWrittenByVLC->textBrush
                );

                textureJustWrittenByVLC->d2dRenderTarget->SetTransform(originalTransform);
            }

            textureJustWrittenByVLC->d2dRenderTarget->EndDraw();
        }
    }
    else
    {
        DEBUG_VERBOSE("[D3D11] Swap: skipped drawing (missing resources)");
    }
#endif // SHOW_WATERMARK

    m_textureForUnity = textureJustWrittenByVLC;
    m_updated = true;
    DEBUG_VERBOSE("[D3D11] Swap: m_textureForUnity set, m_updated=true");

    if (current_texture == read_write[0].get()) {
        current_texture = read_write[1].get();
    } else {
        current_texture = read_write[0].get();
    }

    LeaveCriticalSection(&m_outputLock);
}

bool RenderAPI_D3D11::MakeCurrent(bool enter)
{
    if (m_d3dctxVLC == NULL) return false;

    if (enter)
    {
        EnterCriticalSection(&m_outputLock);
        if (current_texture && current_texture->m_textureRenderTarget)
        {
            m_d3dctxVLC->ClearRenderTargetView(current_texture->m_textureRenderTarget, blackRGBA);
        }
        return true;
    }
    LeaveCriticalSection(&m_outputLock);
    return true;
}

bool RenderAPI_D3D11::SelectPlane(size_t plane, void *output)
{
    (void)output;
    if (plane != 0 || m_d3dctxVLC == NULL) // we only support one packed RGBA plane
        return false;

    if (current_texture && current_texture->m_textureRenderTarget)
    {
        m_d3dctxVLC->OMSetRenderTargets(1, &current_texture->m_textureRenderTarget, NULL);
    }

    return true;
}

bool RenderAPI_D3D11::Setup(const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out)
{
    (void)cfg;
    DEBUG("Setup m_d3dctxVLC = %p this = %p \n", m_d3dctxVLC, this);

#if defined(SHOW_WATERMARK)
    // Randomly select watermark position
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 2);

    int randomPosition = dis(gen);
    m_watermarkPosition = static_cast<WatermarkPosition>(randomPosition);
#endif // SHOW_WATERMARK

    if (out && m_d3dctxVLC)
    {
        out->d3d11.device_context = m_d3dctxVLC;
        return true;
    }
    return false;
}

void RenderAPI_D3D11::Report(libvlc_video_output_resize_cb report_size_change,
                             libvlc_video_output_mouse_move_cb report_mouse_move,
                             libvlc_video_output_mouse_press_cb report_mouse_press,
                             libvlc_video_output_mouse_release_cb report_mouse_release,
                             void *report_opaque)
{
    (void)report_mouse_move;
    (void)report_mouse_press;
    (void)report_mouse_release;
    DEBUG("Resize_cb called \n");
    EnterCriticalSection(&m_sizeLock);
    m_ReportSize = report_size_change;
    m_reportOpaque = report_opaque;

    if (m_ReportSize != nullptr)
    {
        DEBUG("Invoking m_ReportSize(m_reportOpaque, m_width, m_height) with width=%u and height=%u \n", m_width, m_height);
        m_ReportSize(m_reportOpaque, m_width, m_height);
    }
    LeaveCriticalSection(&m_sizeLock);

    DEBUG("Exiting Resize_cb");
}

void* RenderAPI_D3D11::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    void* result = nullptr;
    bool local_updated_status = false;

    EnterCriticalSection(&m_outputLock);

    if (m_textureForUnity) {
        result = m_textureForUnity->GetUnityTexture();
    }
    local_updated_status = m_updated;
    m_updated = false;

#if defined(SHOW_WATERMARK)
    bool isStopped = libvlc_unity_trial_is_stopped();
    DEBUG_VERBOSE("[D3D11] getVideoFrame: isStopped=%d, m_trialMessageDrawn=%d, width=%u, height=%u", isStopped, m_trialMessageDrawn, width, height);

    if (isStopped && width > 0 && height > 0)
    {
        if (ensureTrialTexture(width, height) && m_trialD2DTarget && m_trialBrush && dwriteFactory)
        {
            if (!m_trialMessageDrawn)
            {
                DEBUG_VERBOSE("[D3D11] getVideoFrame: drawing trial message to independent texture");

                m_trialD2DTarget->BeginDraw();

                D2D1_MATRIX_3X2_F originalTransform;
                m_trialD2DTarget->GetTransform(&originalTransform);

                m_trialD2DTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

                const WCHAR line1[] = L"Trial session expired";
                const WCHAR line2[] = L"Purchase the pro version at https://videolabs.io/store/unity";
                UINT32 line1Length = ARRAYSIZE(line1) - 1;
                UINT32 line2Length = ARRAYSIZE(line2) - 1;

                float maxWidth = width * 0.9f;
                float line1FontSize = maxWidth / (line1Length * 0.55f);
                float line2FontSize = maxWidth / (line2Length * 0.55f);
                if (line1FontSize > 72.0f) line1FontSize = 72.0f;
                if (line2FontSize > 36.0f) line2FontSize = 36.0f;
                float lineSpacing = line1FontSize * 0.5f;

                float totalHeight = line1FontSize + lineSpacing + line2FontSize;
                float startY = (height - totalHeight) / 2.0f;

                D2D1_RECT_F line1Rect = D2D1::RectF(0, startY, (float)width, startY + line1FontSize * 1.2f);
                D2D1_RECT_F line2Rect = D2D1::RectF(0, startY + line1FontSize + lineSpacing,
                    (float)width, startY + line1FontSize + lineSpacing + line2FontSize * 1.2f);

                float centerX = width / 2.0f;
                D2D1_MATRIX_3X2_F mirror =
                    D2D1::Matrix3x2F::Translation(-centerX, 0.0f) *
                    D2D1::Matrix3x2F::Scale(-1.0f, 1.0f, D2D1::Point2F(0.0f, 0.0f)) *
                    D2D1::Matrix3x2F::Translation(centerX, 0.0f);
                m_trialD2DTarget->SetTransform(mirror * originalTransform);

                IDWriteTextFormat* line1Format = nullptr;
                IDWriteTextFormat* line2Format = nullptr;
                dwriteFactory->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, line1FontSize, L"en-us", &line1Format);
                dwriteFactory->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, line2FontSize, L"en-us", &line2Format);
                if (line1Format) line1Format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                if (line2Format) line2Format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

                m_trialD2DTarget->DrawText(line1, line1Length, line1Format, line1Rect, m_trialBrush);
                m_trialD2DTarget->DrawText(line2, line2Length, line2Format, line2Rect, m_trialBrush);

                if (line1Format) line1Format->Release();
                if (line2Format) line2Format->Release();

                m_trialD2DTarget->SetTransform(originalTransform);
                m_trialD2DTarget->EndDraw();
                m_trialMessageDrawn = true;
                DEBUG_VERBOSE("[D3D11] getVideoFrame: trial message drawn successfully");
            }

            result = (void*)m_trialTextureView;
            local_updated_status = true;
        }
    }
    else if (!isStopped && m_trialMessageDrawn)
    {
        m_trialMessageDrawn = false;
        DEBUG_VERBOSE("[D3D11] getVideoFrame: reset m_trialMessageDrawn");
    }
#endif // SHOW_WATERMARK

    LeaveCriticalSection(&m_outputLock);

    *out_updated = local_updated_status;

    if (m_width != width || m_height != height)
    {
        EnterCriticalSection(&m_sizeLock);
        if (m_ReportSize != nullptr && (this->m_width != width || this->m_height != height))
        {
             m_ReportSize(m_reportOpaque, width, height);
        }
        LeaveCriticalSection(&m_sizeLock);
    }

    return result;
}

#if defined(SHOW_WATERMARK)
bool RenderAPI_D3D11::ensureTrialTexture(unsigned width, unsigned height)
{
    if (m_trialTexture && m_trialTextureWidth == width && m_trialTextureHeight == height)
        return true;

    releaseTrialTexture();

    if (width == 0 || height == 0)
        return false;

    ID3D11Device* d3dDevice = nullptr;
    HRESULT hr = m_d3deviceUnity->QueryInterface(&d3dDevice);
    if (FAILED(hr) || !d3dDevice)
        return false;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = d3dDevice->CreateTexture2D(&texDesc, nullptr, &m_trialTexture);
    if (FAILED(hr)) {
        d3dDevice->Release();
        return false;
    }

    hr = d3dDevice->CreateShaderResourceView(m_trialTexture, nullptr, &m_trialTextureView);
    if (FAILED(hr)) {
        m_trialTexture->Release();
        m_trialTexture = nullptr;
        d3dDevice->Release();
        return false;
    }

    d3dDevice->Release();

    if (d2dFactory) {
        IDXGISurface* dxgiSurface = nullptr;
        hr = m_trialTexture->QueryInterface(&dxgiSurface);
        if (SUCCEEDED(hr) && dxgiSurface) {
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );
            hr = d2dFactory->CreateDxgiSurfaceRenderTarget(dxgiSurface, props, &m_trialD2DTarget);
            dxgiSurface->Release();

            if (SUCCEEDED(hr) && m_trialD2DTarget) {
                m_trialD2DTarget->CreateSolidColorBrush(
                    D2D1::ColorF(255.0f/255.0f, 102.0f/255.0f, 0.0f/255.0f, 1.0f),
                    &m_trialBrush
                );
            }
        }
    }

    m_trialTextureWidth = width;
    m_trialTextureHeight = height;
    DEBUG("[D3D11] Trial texture created: %ux%u", width, height);
    return true;
}

void RenderAPI_D3D11::releaseTrialTexture()
{
    if (m_trialBrush) { m_trialBrush->Release(); m_trialBrush = nullptr; }
    if (m_trialD2DTarget) { m_trialD2DTarget->Release(); m_trialD2DTarget = nullptr; }
    if (m_trialTextureView) { m_trialTextureView->Release(); m_trialTextureView = nullptr; }
    if (m_trialTexture) { m_trialTexture->Release(); m_trialTexture = nullptr; }
    m_trialTextureWidth = 0;
    m_trialTextureHeight = 0;
}
#endif

void RenderAPI_D3D11::setColorSpace(int color_space)
{
    m_linear = color_space == 1;
}

void RenderAPI_D3D11::setbitDepthFormat(int bit_depth)
{
    m_bit_depth = bit_depth;
}

// #endif // #if SUPPORT_D3D11
