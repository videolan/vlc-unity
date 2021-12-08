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

#define SCREEN_WIDTH  100
#define SCREEN_HEIGHT  100

class ReadWriteTexture
{
public:
    ID3D11Texture2D          *m_textureUnity        = nullptr;
    ID3D11ShaderResourceView *m_textureShaderInput  = nullptr;
    HANDLE                   m_sharedHandle         = nullptr; // handle of the texture used by VLC and the app
    ID3D11RenderTargetView   *m_textureRenderTarget = nullptr;

    void Cleanup();
    void Update(unsigned width, unsigned height, ID3D11Device *m_d3deviceUnity, ID3D11Device *m_d3deviceVLC);
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
    bool UpdateOutput( const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *out );
    void Swap();
    bool MakeCurrent(bool enter );
    bool SelectPlane(size_t plane, void *output);
    bool Setup(const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out );
    void Resize(void (*report_size_change)(void *report_opaque, unsigned width, unsigned height), void *report_opaque );

    ReadWriteTexture        *read_write[2];
    bool                    write_on_first = false;

private:
    void CreateResources();
    void ReleaseResources();
    void DebugInUnity(LPCSTR message);
    void Update(UINT width, UINT height);

    /* Unity side resources */
    ID3D11Device             *m_d3deviceUnity       = nullptr;

    /* VLC side resources */
    ID3D11Device            *m_d3deviceVLC          = nullptr;
    ID3D11DeviceContext     *m_d3dctxVLC            = nullptr;

    CRITICAL_SECTION m_sizeLock; // the ReportSize callback cannot be called during/after the Cleanup_cb is called
    unsigned m_width = 0;
    unsigned m_height = 0;
    void (*m_ReportSize)(void *ReportOpaque, unsigned width, unsigned height) = nullptr;
    void *m_reportOpaque = nullptr;

    bool m_updated = false;

    CRITICAL_SECTION m_outputLock; // the ReportSize callback cannot be called during/after the Cleanup_cb is called

    bool m_initialized = false;
    bool m_linear = false;
};

// VLC C-style callbacks
bool UpdateOutput_cb( void *opaque, const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *out )
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    return me->UpdateOutput(cfg, out);
}

void Swap_cb( void* opaque )
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    me->Swap();
}

bool MakeCurrent_cb( void *opaque, bool enter )
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    return me->MakeCurrent(enter);
}

bool SelectPlane_cb( void *opaque, size_t plane, void *output )
{
    return ((RenderAPI_D3D11*)opaque)->SelectPlane(plane, output);
}

bool Setup_cb( void **opaque, const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out )
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(*opaque);
    return me->Setup(cfg, out);
}

void Cleanup_cb( void *opaque )
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    me->read_write[0]->Cleanup();
    me->read_write[1]->Cleanup();
}

void Resize_cb( void *opaque,
                    void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                    void *report_opaque )
{
    RenderAPI_D3D11 *me = reinterpret_cast<RenderAPI_D3D11*>(opaque);
    me->Resize(report_size_change, report_opaque);
}

RenderAPI* CreateRenderAPI_D3D11()
{
    return new RenderAPI_D3D11();
}

RenderAPI_D3D11::RenderAPI_D3D11()
{
    read_write[0] = new ReadWriteTexture();
    read_write[1] = new ReadWriteTexture();
}

RenderAPI_D3D11::~RenderAPI_D3D11()
{
    delete read_write[0];
    delete read_write[1];
}

void RenderAPI_D3D11::setVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[D3D11] Subscribing output callbacks %p \n", this);

    libvlc_video_set_output_callbacks( mp, libvlc_video_engine_d3d11,
                                    Setup_cb, Cleanup_cb, Resize_cb, UpdateOutput_cb,
                                    Swap_cb, MakeCurrent_cb, nullptr, nullptr, SelectPlane_cb,
                                    this);

    CreateResources();
}

void RenderAPI_D3D11::unsetVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[D3D11] Unsubscribing to output callbacks %p \n", this);

    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_disable,
                                    Setup_cb, Cleanup_cb, Resize_cb, UpdateOutput_cb,
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
            if(d3d == NULL)
            {
                DEBUG("Could not retrieve IUnityGraphicsD3D11 \n");
                return;
            }
            m_d3deviceUnity = d3d->GetDevice();
            if(m_d3deviceUnity == NULL)
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
    read_write[0]->Update(m_width, m_height, m_d3deviceUnity, m_d3deviceVLC);
    read_write[1]->Update(m_width, m_height, m_d3deviceUnity, m_d3deviceVLC);

    LeaveCriticalSection(&m_outputLock);
}

void ReadWriteTexture::Update(UINT width, UINT height, ID3D11Device *m_d3deviceUnity, ID3D11Device *m_d3deviceVLC)
{
    Cleanup();

    DEBUG("Done releasing d3d objects.\n");

    /* interim texture */
    D3D11_TEXTURE2D_DESC texDesc = { 0 };
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.CPUAccessFlags = 0;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.Height = height;
    texDesc.Width  = width;
    
    texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    
    HRESULT hr;
    hr = m_d3deviceUnity->CreateTexture2D( &texDesc, NULL, &m_textureUnity );
    if (FAILED(hr))
    {
        DEBUG("CreateTexture2D FAILED \n");
    }
    else
    {
        DEBUG("CreateTexture2D SUCCEEDED.\n");
    }

    IDXGIResource1* sharedResource = NULL;

    hr = m_textureUnity->QueryInterface(&sharedResource);
    if(FAILED(hr))
    {
        DEBUG("get IDXGIResource1 FAILED \n");
    }

    hr = sharedResource->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &m_sharedHandle);
    if(FAILED(hr))
    {
        _com_error error(hr);
        DEBUG("sharedResource->CreateSharedHandle FAILED %s \n", error.ErrorMessage());
    }

    sharedResource->Release();
    sharedResource = NULL;

    ID3D11Device1* d3d11VLC1;
    hr = m_d3deviceVLC->QueryInterface(&d3d11VLC1);
    if(FAILED(hr))
    {
        _com_error error(hr);
        DEBUG("QueryInterface ID3D11Device1 FAILED %s \n", error.ErrorMessage());
    }
    
    ID3D11Texture2D* textureVLC;
    hr = d3d11VLC1->OpenSharedResource1(m_sharedHandle, __uuidof(ID3D11Texture2D), (void**)&textureVLC);
    if(FAILED(hr))
    {
        _com_error error(hr);
        DEBUG("ctx->d3device->OpenSharedResource FAILED %s \n", error.ErrorMessage());
    }

    d3d11VLC1->Release();
    d3d11VLC1 = NULL;

    D3D11_SHADER_RESOURCE_VIEW_DESC resviewDesc;
    ZeroMemory(&resviewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    resviewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resviewDesc.Texture2D.MipLevels = 1;
    resviewDesc.Format = texDesc.Format;
    hr = m_d3deviceUnity->CreateShaderResourceView(m_textureUnity, &resviewDesc, &m_textureShaderInput);
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

    hr = m_d3deviceVLC->CreateRenderTargetView(textureVLC, &renderTargetViewDesc, &m_textureRenderTarget);
    if (FAILED(hr))
    {
        DEBUG("CreateRenderTargetView FAILED \n");
    }

    textureVLC->Release();// No need to keep a reference to that, VLC only writes to the renderTarget
    textureVLC = NULL;
}

void RenderAPI_D3D11::CreateResources()
{
    DEBUG("Entering CreateResources \n");

    if(m_d3deviceUnity == NULL)
    {
        DEBUG("Could not retrieve unity d3device %p, aborting... \n", m_d3deviceUnity);
        return;
    }

    HRESULT hr;

    ZeroMemory(&m_sizeLock, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(&m_sizeLock);
    ZeroMemory(&m_outputLock, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(&m_outputLock);

    UINT creationFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT; /* needed for hardware decoding */
#ifndef NDEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    hr = D3D11CreateDevice(NULL,
                        D3D_DRIVER_TYPE_HARDWARE,
                        NULL,
                        creationFlags,
                        NULL,
                        NULL,
                        D3D11_SDK_VERSION,
                        &m_d3deviceVLC,
                        NULL,
                        &m_d3dctxVLC);
    DEBUG("CreateResources m_d3dctxVLC = %p this = %p \n", m_d3dctxVLC, this);

    if(FAILED(hr))
    {
        _com_error error(hr);
        DEBUG("FAILED to create d3d11 device and context %s \n", error.ErrorMessage());
    }

    DEBUG("Configuring multithread \n");

    /* The ID3D11Device must have multithread protection */
    ID3D10Multithread *pMultithread;
    hr = m_d3dctxVLC->QueryInterface(&pMultithread);
    if (SUCCEEDED(hr)) {
        pMultithread->SetMultithreadProtected(TRUE);
        pMultithread->Release();
    } else {
        _com_error error(hr);
        DEBUG("FAILED to SetMultithreadProtected %s \n", error.ErrorMessage());
    }

    DEBUG("Exiting CreateResources.\n");
}

void ReadWriteTexture::Cleanup()
{
    DEBUG("Entering Cleanup.\n");

    if(m_textureRenderTarget)
    {
        m_textureRenderTarget->Release();
        m_textureRenderTarget = nullptr;
    }
        
    if(m_textureShaderInput)
    {
        m_textureShaderInput->Release();
        m_textureShaderInput = nullptr;
    }

    if(m_textureUnity)
    {
        m_textureUnity->Release();
        m_textureUnity = nullptr;
    }

    if(m_sharedHandle)
    {
        CloseHandle(m_sharedHandle);
        m_sharedHandle = nullptr;
    }
}

void RenderAPI_D3D11::ReleaseResources()
{
    DEBUG("ReleaseResources called \n");

    if(m_d3deviceVLC)
    {
        m_d3deviceVLC->Release();
        m_d3deviceVLC = nullptr;
    }
    
    if(m_d3dctxVLC)
    {
        m_d3dctxVLC->Release();
        m_d3dctxVLC = nullptr;
    }
}

bool RenderAPI_D3D11::UpdateOutput( const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *out )
{
    DEBUG("Entering UpdateOutput_cb.\n");

    DXGI_FORMAT renderFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    Update(cfg->width, cfg->height);

    out->dxgi_format = renderFormat;
    out->full_range     = true;
    out->colorspace     = libvlc_video_colorspace_BT709;
    out->primaries      = libvlc_video_primaries_BT709;
    out->transfer       = m_linear ? libvlc_video_transfer_func_LINEAR : libvlc_video_transfer_func_SRGB;

    DEBUG("Exiting UpdateOutput_cb \n");

    return true;
}

void RenderAPI_D3D11::Swap()
{
    EnterCriticalSection(&m_outputLock);
    m_updated = true;
    write_on_first = !write_on_first;
    LeaveCriticalSection(&m_outputLock);
}

bool RenderAPI_D3D11::MakeCurrent( bool enter )
{
    return true;
}

bool RenderAPI_D3D11::SelectPlane( size_t plane, void *output )
{
    if ( plane != 0 || m_d3dctxVLC == NULL ) // we only support one packed RGBA plane (DXGI_FORMAT_R8G8B8A8_UNORM)
        return false;
    
    size_t write_index = write_on_first ? 0 : 1;

    static const FLOAT blackRGBA[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    
    m_d3dctxVLC->OMSetRenderTargets( 1, &read_write[write_index]->m_textureRenderTarget, NULL );
    m_d3dctxVLC->ClearRenderTargetView( read_write[write_index]->m_textureRenderTarget, blackRGBA);

    return true;
}

bool RenderAPI_D3D11::Setup( const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out )
{
    DEBUG("Setup m_d3dctxVLC = %p this = %p \n", m_d3dctxVLC, this);
    out->d3d11.device_context = m_d3dctxVLC;
    return true;
}

void RenderAPI_D3D11::Resize(void (*report_size_change)(void *report_opaque, unsigned width, unsigned height),
                       void *report_opaque )
{
    DEBUG("Resize_cb called \n");
    EnterCriticalSection(&m_sizeLock);
    m_ReportSize = report_size_change;
    m_reportOpaque = report_opaque;

    if (m_ReportSize != nullptr)
    {
        DEBUG("Invoking m_ReportSize(m_reportOpaque, m_width, m_height) with width=%u and height=%u \n", m_width, m_height);

        /* report our initial size */
        m_ReportSize(m_reportOpaque, m_width, m_height);
    }
    LeaveCriticalSection(&m_sizeLock);

    DEBUG("Exiting Resize_cb");
}

void* RenderAPI_D3D11::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    void* result;
    EnterCriticalSection(&m_outputLock);
   
    *out_updated = m_updated;
    m_updated = false;
    size_t read_index = write_on_first ? 1 : 0;
    result = read_write[read_index]->m_textureShaderInput;

    if(m_width != width || m_height != height)
    {
        m_width = width;
        m_height = height;

        EnterCriticalSection(&m_sizeLock);

        if (m_ReportSize != nullptr)
            m_ReportSize(m_reportOpaque, m_width, m_height);

        LeaveCriticalSection(&m_sizeLock);
    }

    LeaveCriticalSection(&m_outputLock);
    return result;
}

void RenderAPI_D3D11::setColorSpace(int color_space)
{
    m_linear = color_space == 1;
}

// #endif // #if SUPPORT_D3D11
