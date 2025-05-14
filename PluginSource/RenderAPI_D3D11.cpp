#include "RenderAPI.h"
#include "PlatformBase.h"

// Direct3D 11 implementation of RenderAPI.

// #if SUPPORT_D3D11

#include <assert.h>
#include <tchar.h>
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#if defined(SHOW_WATERMARK)
#include <d2d1.h>
#include <dwrite.h>
#endif // SHOW_WATERMARK
#include "Unity/IUnityGraphicsD3D11.h"
#include "Log.h"

#include <algorithm>
#include <dxgi1_2.h>

#include <memory>

#if !defined(UWP)
// #include <comdef.h> // enable for debugging
#endif

#define SCREEN_WIDTH  100
#define SCREEN_HEIGHT  100

class ReadWriteTexture
{
public:
    ID3D11Texture2D          *m_textureUnity        = nullptr;
    ID3D11ShaderResourceView *m_textureShaderInput  = nullptr;
    HANDLE                   m_sharedHandle         = nullptr; // handle of the texture used by VLC and the app
    ID3D11RenderTargetView   *m_textureRenderTarget = nullptr;
    unsigned                 width                  = 0;       // Texture width
    unsigned                 height                 = 0;       // Texture height
#if defined(SHOW_WATERMARK)
    ID2D1RenderTarget        *d2dRenderTarget       = nullptr; // Per-texture D2D render target
    ID2D1SolidColorBrush     *textBrush             = nullptr; // Per-texture text brush
#endif // SHOW_WATERMARK

    void Cleanup();
    void Update(unsigned width, unsigned height, ID3D11Device *m_d3deviceUnity, ID3D11Device *m_d3deviceVLC
#if defined(SHOW_WATERMARK)
                , ID2D1Factory *d2dFactory, IDWriteFactory *dwriteFactory, IDWriteTextFormat *textFormat
#endif // SHOW_WATERMARK
                );
};

class RenderAPI_D3D11 : public RenderAPI
{
public:
    RenderAPI_D3D11();
    ~RenderAPI_D3D11();

    virtual void setVlcContext(libvlc_media_player_t *mp) override;
    virtual void unsetVlcContext(libvlc_media_player_t *mp) override;
    virtual void ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces) override;
    void* getVideoFrame(unsigned width, unsigned height, bool* out_updated) override;
    void setColorSpace(int color_space) override;

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

    /* Unity side resources */
    ID3D11Device             *m_d3deviceUnity       = nullptr;

    /* VLC side resources */
    ID3D11Device            *m_d3deviceVLC          = nullptr;
    ID3D11DeviceContext     *m_d3dctxVLC            = nullptr;

#if defined(SHOW_WATERMARK)
    /* Videolabs watermark */
    ID2D1Factory            *d2dFactory             = nullptr;
    IDWriteFactory          *dwriteFactory          = nullptr;
    IDWriteTextFormat       *textFormat             = nullptr;
    float fontSize = 124.0f;
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

RenderAPI* CreateRenderAPI_D3D11()
{
    return new RenderAPI_D3D11();
}

RenderAPI_D3D11::RenderAPI_D3D11()
{
    read_write[0].reset(new ReadWriteTexture());
    read_write[1].reset(new ReadWriteTexture());
    current_texture = read_write[0].get();
    m_textureForUnity = nullptr;
}

RenderAPI_D3D11::~RenderAPI_D3D11()
{
    DeleteCriticalSection(&m_sizeLock);
    DeleteCriticalSection(&m_outputLock);
}

void RenderAPI_D3D11::setVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[D3D11] Subscribing output callbacks %p \n", this);

    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_d3d11,
                                      Setup_cb, Cleanup_cb, Report_cb, UpdateOutput_cb,
                                      Swap_cb, MakeCurrent_cb, nullptr, nullptr, SelectPlane_cb,
                                      this);

    CreateResources();
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
            IUnityGraphicsD3D11* d3d = interfaces->Get<IUnityGraphicsD3D11>();
            if (d3d == NULL)
            {
                DEBUG("Could not retrieve IUnityGraphicsD3D11 \n");
                return;
            }
            m_d3deviceUnity = d3d->GetDevice();
            if (m_d3deviceUnity == NULL)
            {
                DEBUG("Could not retrieve d3device \n");
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

void ReadWriteTexture::Update(unsigned width, unsigned height, ID3D11Device *m_d3deviceUnity, ID3D11Device *m_d3deviceVLC
#if defined(SHOW_WATERMARK)
                              , ID2D1Factory *d2dFactory, IDWriteFactory *dwriteFactory, IDWriteTextFormat *textFormat
#endif // SHOW_WATERMARK
                             )
{
    Cleanup();

    DEBUG("Done releasing d3d objects.\n");

    this->width = width;
    this->height = height;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.CPUAccessFlags = 0;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.Height = height;
    texDesc.Width = width;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    HRESULT hr;
    hr = m_d3deviceUnity->CreateTexture2D(&texDesc, NULL, &m_textureUnity);
    if (FAILED(hr))
    {
        DEBUG("CreateTexture2D FAILED \n");
        return;
    }
    DEBUG("CreateTexture2D SUCCEEDED.\n");

    IDXGIResource1* sharedResource = NULL;
    hr = m_textureUnity->QueryInterface(&sharedResource);
    if (FAILED(hr))
    {
        DEBUG("get IDXGIResource1 FAILED \n");
        m_textureUnity->Release();
        m_textureUnity = nullptr;
        return;
    }

    hr = sharedResource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &m_sharedHandle);
    if (FAILED(hr))
    {
        DEBUG("sharedResource->CreateSharedHandle FAILED \n");
        sharedResource->Release();
        m_textureUnity->Release();
        m_textureUnity = nullptr;
        return;
    }
    sharedResource->Release();
    sharedResource = NULL;

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


    D3D11_SHADER_RESOURCE_VIEW_DESC resviewDesc;
    ZeroMemory(&resviewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    resviewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resviewDesc.Texture2D.MipLevels = 1;
    resviewDesc.Format = texDesc.Format;
    hr = m_d3deviceUnity->CreateShaderResourceView(m_textureUnity, &resviewDesc, &m_textureShaderInput);
    if (FAILED(hr))
    {
        DEBUG("CreateShaderResourceView FAILED \n");
        if(textureVLC) textureVLC->Release();
        m_textureUnity->Release();
        m_textureUnity = nullptr;
        return;
    }
    DEBUG("CreateShaderResourceView SUCCEEDED.\n");

    if (textureVLC && m_d3deviceVLC)
    {
        D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
        ZeroMemory(&renderTargetViewDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
        renderTargetViewDesc.Format = texDesc.Format;
        renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        hr = m_d3deviceVLC->CreateRenderTargetView(textureVLC, &renderTargetViewDesc, &m_textureRenderTarget);
        if (FAILED(hr))
        {
            DEBUG("CreateRenderTargetView FAILED \n");
            m_textureShaderInput->Release();
            m_textureShaderInput = nullptr;
            m_textureUnity->Release();
            m_textureUnity = nullptr;
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


void RenderAPI_D3D11::CreateResources()
{
    DEBUG("Entering CreateResources \n");

    if (m_d3deviceUnity == NULL)
    {
        DEBUG("Could not retrieve unity d3device %p, aborting... \n", m_d3deviceUnity);
        return;
    }

    HRESULT hr;

    ZeroMemory(&m_sizeLock, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(&m_sizeLock);
    ZeroMemory(&m_outputLock, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(&m_outputLock);

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
    if (m_textureShaderInput) {
        m_textureShaderInput->Release();
        m_textureShaderInput = nullptr;
    }
    if (m_textureUnity) {
        m_textureUnity->Release();
        m_textureUnity = nullptr;
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

    DXGI_FORMAT renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
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
    if (textureJustWrittenByVLC && textureJustWrittenByVLC->d2dRenderTarget && textFormat && textureJustWrittenByVLC->textBrush) {
        textureJustWrittenByVLC->d2dRenderTarget->BeginDraw();

        D2D1_MATRIX_3X2_F originalTransform;
        textureJustWrittenByVLC->d2dRenderTarget->GetTransform(&originalTransform);

        const WCHAR watermarkText[] = L"Videolabs.io";
        UINT32 textLength = ARRAYSIZE(watermarkText) - 1;

        float padding = 10.0f;
        float approxTextWidth = textLength * (fontSize * 0.5f);
        float textRectLeft = (textureJustWrittenByVLC->width - approxTextWidth) / 2.0f;
        float textRectTop = textureJustWrittenByVLC->height - fontSize - padding;
        float textRectRight = textRectLeft + approxTextWidth;
        float textRectBottom = textureJustWrittenByVLC->height - padding;

        D2D1_RECT_F textRect = D2D1::RectF(
            textRectLeft,
            textRectTop,
            textRectRight,
            textRectBottom
        );

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

        HRESULT hr = textureJustWrittenByVLC->d2dRenderTarget->EndDraw();
        if (FAILED(hr)) {
            DEBUG("Direct2D EndDraw FAILED: 0x%lx\n", hr);
        }
    } else {
         DEBUG("Watermark rendering skipped (either disabled or D2D resources missing)\n");
    }
#endif // SHOW_WATERMARK

    m_textureForUnity = textureJustWrittenByVLC;
    m_updated = true;

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
    if (plane != 0 || m_d3dctxVLC == NULL) // we only support one packed RGBA plane (DXGI_FORMAT_R8G8B8A8_UNORM)
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
        result = m_textureForUnity->m_textureShaderInput;
    }
    local_updated_status = m_updated;
    m_updated = false;

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

void RenderAPI_D3D11::setColorSpace(int color_space)
{
    m_linear = color_space == 1;
}

// #endif // #if SUPPORT_D3D11