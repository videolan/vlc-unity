#include "PlatformBase.h"
#include "RenderAPI.h"
#include "Log.h"
#include "TrialWatermark.h"

#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

#if defined(SHOW_WATERMARK)
static std::atomic<int64_t> g_trialAccumulatedMs{0};
static std::atomic<int64_t> g_trialLastTickMs{-1};
static std::atomic<bool> g_trialIsPaused{false};
static std::atomic<bool> g_trialIsStopped{true};
static const int64_t TRIAL_TIME_LIMIT_MS = 30 * 1000;
#endif

#if defined(SUPPORT_D3D11)
#include <windows.h>
#endif

#if defined(SUPPORT_VULKAN)
#include "VulkanPlatformRequirements.h"
#endif

extern "C" {
#include <stdlib.h>
#if !defined(_WIN32)
#include <unistd.h>
#include <pthread.h>
#endif
#include <vlc/vlc.h>
#include <string.h>
}

static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

libvlc_instance_t * inst;

#if defined(SHOW_WATERMARK)
static void trial_reset();
static void trial_pause();
static bool trial_is_expired();

static void on_media_player_state_changed(void* opaque, libvlc_state_t state)
{
    (void)opaque;
    switch (state)
    {
    case libvlc_Stopped:
        DEBUG("[Trial] Event: MediaPlayerStopped");
        g_trialIsStopped.store(true);
        g_trialIsPaused.store(false);
        trial_reset();
        break;
    case libvlc_Paused:
        DEBUG("[Trial] Event: MediaPlayerPaused");
        g_trialIsPaused.store(true);
        trial_pause();
        break;
    case libvlc_Playing:
        DEBUG("[Trial] Event: MediaPlayerPlaying");
        g_trialIsStopped.store(false);
        g_trialIsPaused.store(false);
        break;
    default:
        break;
    }
}

static struct libvlc_media_player_cbs create_media_player_callbacks()
{
    struct libvlc_media_player_cbs callbacks = {};
    callbacks.version = 0;
    callbacks.on_state_changed = on_media_player_state_changed;
    return callbacks;
}

static const struct libvlc_media_player_cbs media_player_callbacks = create_media_player_callbacks();

using MediaPlayerStateChangedCallback = void (*)(void*, libvlc_state_t);
static std::atomic<MediaPlayerStateChangedCallback> managed_media_player_state_changed{nullptr};

static void on_media_player_state_changed_with_trial(void* opaque, libvlc_state_t state)
{
    on_media_player_state_changed(opaque, state);

    auto managed_state_changed = managed_media_player_state_changed.load(std::memory_order_acquire);
    if (managed_state_changed != nullptr)
        managed_state_changed(opaque, state);
}

static const struct libvlc_media_player_cbs* callbacks_with_trial_state(
    const struct libvlc_media_player_cbs* callbacks)
{
    if (callbacks == nullptr)
        return &media_player_callbacks;

    static struct libvlc_media_player_cbs combined_callbacks;
    static const struct libvlc_media_player_cbs* source_callbacks = nullptr;
    static std::mutex callbacks_mutex;

    std::lock_guard<std::mutex> lock(callbacks_mutex);
    if (source_callbacks != callbacks ||
        managed_media_player_state_changed.load(std::memory_order_relaxed) != callbacks->on_state_changed)
    {
        combined_callbacks = *callbacks;
        managed_media_player_state_changed.store(callbacks->on_state_changed, std::memory_order_release);
        combined_callbacks.on_state_changed = on_media_player_state_changed_with_trial;
        source_callbacks = callbacks;
    }

    return &combined_callbacks;
}
#endif

static IUnityGraphics* s_Graphics = NULL;
static std::map<libvlc_media_player_t*,RenderAPI*> contexts = {};
static std::vector<RenderAPI*> retiredContexts;
// Unity invokes plugin exports and render events from different threads.
// One mutex is enough to keep renderer lookup, retirement, and destruction
// mutually exclusive without a separate registry abstraction.
static std::mutex s_contextsMutex;
static RenderAPI* EarlyRenderAPI = NULL;
static IUnityInterfaces* s_UnityInterfaces = NULL;

static int s_color_space;

enum RenderEventId
{
    kVulkanCopyEvent = 0,
    kRenderThreadWorkEvent = 1,
    kVulkanQueueSubmissionEvent = 2,
    kRendererCleanupEvent = 3,
};

// Helper function to convert UnityGfxRenderer enum to string
static const char* GetRendererName(UnityGfxRenderer renderer) {
    switch (renderer) {
        case kUnityGfxRendererOpenGL: return "OpenGL";
        case kUnityGfxRendererD3D9: return "D3D9";
        case kUnityGfxRendererD3D11: return "D3D11";
        case kUnityGfxRendererGCM: return "GCM";
        case kUnityGfxRendererNull: return "Null";
        case kUnityGfxRendererXenon: return "Xenon";
        case kUnityGfxRendererOpenGLES20: return "OpenGLES20";
        case kUnityGfxRendererOpenGLES30: return "OpenGLES30";
        case kUnityGfxRendererGXM: return "GXM";
        case kUnityGfxRendererPS4: return "PS4";
        case kUnityGfxRendererXboxOne: return "XboxOne";
        case kUnityGfxRendererMetal: return "Metal";
        case kUnityGfxRendererOpenGLCore: return "OpenGLCore";
        case kUnityGfxRendererD3D12: return "D3D12";
        case kUnityGfxRendererVulkan: return "Vulkan";
        default: return "Unknown";
    }
}

/** LibVLC's API function exported to Unity
 *
 * Every following functions will be exported to. Unity We have to
 * redeclare the LibVLC's function for the keyword
 * UNITY_INTERFACE_EXPORT and UNITY_INTERFACE_API
 */

#if defined(__APPLE__)
# import <TargetConditionals.h>
# include <cstdlib>
#elif defined(UNITY_LINUX)
# include <cstdlib>
#endif

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetPluginPath(char* path)
{
    (void)path;
#if defined(SUPPORT_D3D11) && !defined(UWP)
    DEBUG("SetPluginPath \n");
    DEBUG("_putenv_s with VLC_PLUGIN_PATH -> %s \n", path);
    auto e = _putenv_s("VLC_PLUGIN_PATH", path);
    if(e != 0)
        DEBUG("_putenv_s failed \n");
    else DEBUG("_putenv_s succeeded \n");
#elif defined(__APPLE__) && !TARGET_OS_IPHONE
    DEBUG("SetPluginPath \n");
    DEBUG("setenv with VLC_PLUGIN_PATH -> %s \n", path);
    auto e = setenv("VLC_PLUGIN_PATH", path, 1);
    if(e != 0)
        DEBUG("setenv failed \n");
    else DEBUG("setenv succeeded \n");
#elif defined(UNITY_LINUX)
    DEBUG("SetPluginPath \n");
    DEBUG("setenv with VLC_PLUGIN_PATH -> %s \n", path);
    auto e = setenv("VLC_PLUGIN_PATH", path, 1);
    if(e != 0)
        DEBUG("setenv failed \n");
    else DEBUG("setenv succeeded \n");
#endif
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_set_color_space(int color_space)
{
    s_color_space = color_space;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_set_bit_depth_format(libvlc_media_player_t* mp, int bit_depth)
{
#if !defined(SUPPORT_D3D11)
    return;
#endif
    if(mp == NULL)
        return;

    if(bit_depth != 8 /* && bit_depth != 10 */ && bit_depth != 16)
        return;

    std::lock_guard<std::mutex> lock(s_contextsMutex);
    auto context = contexts.find(mp);
    if (context == contexts.end())
        return;
    RenderAPI* s_CurrentAPI = context->second;
    if(!s_CurrentAPI)
    {
        return;
    }

    s_CurrentAPI->setbitDepthFormat(bit_depth);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Print(char* toPrint)
{
    DEBUG("%s", toPrint);
}

extern "C" libvlc_media_player_t* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_media_player_new(libvlc_instance_t* libvlc,
                              const struct libvlc_media_player_cbs* callbacks,
                              void* callbacks_opaque)
{
    if(libvlc == NULL)
    {
        DEBUG("libvlc is NULL, aborting...");
        return NULL;
    }

    inst = libvlc;

    DEBUG("LAUNCH");

    if (inst == NULL) {
        DEBUG("LibVLC is not instanciated");
        return NULL;
    }

    libvlc_media_player_t * mp;

    const struct libvlc_media_player_cbs* effective_callbacks = callbacks;
#if defined(SHOW_WATERMARK)
    effective_callbacks = callbacks_with_trial_state(callbacks);
#endif
    mp = libvlc_media_player_new(inst, effective_callbacks, callbacks_opaque);

    RenderAPI* s_CurrentAPI = nullptr;

    if (mp == NULL) {
        DEBUG("Error initializing media player");
        goto err;
    }

    DEBUG("Calling... Initialize Render API \n");
    {
        std::lock_guard<std::mutex> lock(s_contextsMutex);
        if (!s_Graphics) {
            DEBUG("Unity graphics interface is unavailable");
            goto err;
        }
        s_DeviceType = s_Graphics->GetRenderer();
        if(s_DeviceType == kUnityGfxRendererNull)
        {
            DEBUG("s_DeviceType is NULL \n");
            goto err;
        }

        DEBUG("Calling... CreateRenderAPI \n");
        DEBUG("s_DeviceType = %s \n", GetRendererName(s_DeviceType));

        s_CurrentAPI = CreateRenderAPI(s_DeviceType);

        if(s_CurrentAPI == NULL)
        {
            DEBUG("s_CurrentAPI is NULL \n");
            goto err;
        }

        DEBUG("Calling... ProcessDeviceEvent \n");

        s_CurrentAPI->ProcessDeviceEvent(
            kUnityGfxDeviceEventInitialize, s_UnityInterfaces);
        s_CurrentAPI->setColorSpace(s_color_space);

        DEBUG("Calling... setVlcContext s_CurrentAPI=%p mp=%p", s_CurrentAPI, mp);
        s_CurrentAPI->setVlcContext(mp);

        contexts[mp] = s_CurrentAPI;
    }

    return mp;
err:
    delete s_CurrentAPI;
    if ( mp ) {
        // Stop playing
        libvlc_media_player_stop_async (mp);

        // Free the media_player
        libvlc_media_player_release (mp);
        mp = NULL;
    }
    return NULL;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_media_player_release(libvlc_media_player_t* mp)
{
    if(mp == NULL)
        return;

    RenderAPI* s_CurrentAPI = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_contextsMutex);
        auto context = contexts.find(mp);
        if (context != contexts.end()) {
            s_CurrentAPI = context->second;
            if (s_CurrentAPI) {
                s_CurrentAPI->unsetVlcContext(mp);
                s_CurrentAPI->beginShutdown();
                retiredContexts.push_back(s_CurrentAPI);
            }
            contexts.erase(context);
        }
    }

    libvlc_media_player_release(mp);
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_has_retired_renderers()
{
    std::lock_guard<std::mutex> lock(s_contextsMutex);
    return !retiredContexts.empty();
}

extern "C" void* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_get_texture(libvlc_media_player_t* mp, unsigned width, unsigned height, bool * updated)
{
    *updated = false;

    if(mp == NULL)
        return NULL;

#if defined(SHOW_WATERMARK)
    bool isStopped = libvlc_unity_trial_is_stopped();
    if (!libvlc_media_player_is_playing(mp) && !isStopped)
        return NULL;
#else
    if (!libvlc_media_player_is_playing(mp))
        return NULL;
#endif

    if(width == 0 && height == 0)
        return NULL;

    std::lock_guard<std::mutex> lock(s_contextsMutex);
    auto context = contexts.find(mp);
    if (context == contexts.end())
        return nullptr;
    RenderAPI* s_CurrentAPI = context->second;

    if (!s_CurrentAPI) {
        DEBUG("Error, no Render API");
        if (updated)
            *updated = false;
        return nullptr;
    }

    return s_CurrentAPI->getVideoFrame(width, height, updated);
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_set_unity_texture_vulkan(libvlc_media_player_t* mp, void* unityTexturePtr)
{
    (void)unityTexturePtr;
    if(mp == NULL) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: mp is NULL");
        return false;
    }

    std::lock_guard<std::mutex> lock(s_contextsMutex);
    auto it = contexts.find(mp);
    if(it == contexts.end()) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: no context found for mp");
        return false;
    }

    RenderAPI* s_CurrentAPI = it->second;
    if (!s_CurrentAPI) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: s_CurrentAPI is NULL");
        return false;
    }

    if (s_DeviceType == kUnityGfxRendererVulkan)
        return s_CurrentAPI->setUnityTexture(unityTexturePtr);

    DEBUG("libvlc_unity_set_unity_texture_vulkan: not on Vulkan renderer");
    return false;
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API VLCUnity_UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    DEBUG("UnityPluginLoad");
    s_UnityInterfaces = unityInterfaces;
    s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
    s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

#if defined(SUPPORT_VULKAN)
    const auto preloadTime = std::chrono::steady_clock::now().time_since_epoch();
    const auto preloadUs = std::chrono::duration_cast<std::chrono::microseconds>(
        preloadTime).count();
    DEBUG("[Vulkan] plugin preload interception registration at %lld us",
          static_cast<long long>(preloadUs));
    (void)InitializeVulkanInterception(unityInterfaces);
#if defined(UNITY_LINUX)
    IUnityGraphicsVulkan* vulkan =
        unityInterfaces->Get<IUnityGraphicsVulkan>();
    if (vulkan && vulkan->ConfigureEvent) {
        UnityVulkanPluginEventConfig queueEvent = {};
        queueEvent.renderPassPrecondition = kUnityVulkanRenderPass_DontCare;
        queueEvent.graphicsQueueAccess = kUnityVulkanGraphicsQueueAccess_Allow;
        queueEvent.flags =
            kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission |
            kUnityVulkanEventConfigFlag_FlushCommandBuffers;
        vulkan->ConfigureEvent(kVulkanQueueSubmissionEvent, &queueEvent);
    }
#endif
#endif

    // Run OnGraphicsDeviceEvent(initialize) manually on plugin load
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    VLCUnity_UnityPluginLoad(unityInterfaces);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API VLCUnity_UnityPluginUnload()
{
    SetLogCallback(nullptr);

    if (s_Graphics != nullptr)
    {
        s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
        s_Graphics = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(s_contextsMutex);
        for (auto& context : contexts) {
            if (!context.second)
                continue;
            context.second->unsetVlcContext(context.first);
            context.second->prepareForPluginUnload();
            delete context.second;
        }
        contexts.clear();
        for (RenderAPI* retired : retiredContexts) {
            if (!retired)
                continue;
            retired->prepareForPluginUnload();
            delete retired;
        }
        retiredContexts.clear();
        if (EarlyRenderAPI)
            EarlyRenderAPI->prepareForPluginUnload();
        delete EarlyRenderAPI;
        EarlyRenderAPI = nullptr;
    }
    s_UnityInterfaces = nullptr;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
    VLCUnity_UnityPluginUnload();
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
    std::lock_guard<std::mutex> lock(s_contextsMutex);
    // Create graphics API implementation upon initialization
    if (eventType == kUnityGfxDeviceEventInitialize) {
        const auto deviceTime = std::chrono::steady_clock::now().time_since_epoch();
        DEBUG("[Vulkan] graphics-device initialize observed at %lld us",
              static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(deviceTime).count()));
        DEBUG("Initialise Render API");
        if (EarlyRenderAPI != NULL) {
            DEBUG("*** Reinitialising existing EarlyRenderAPI ***");
        } else {
            DEBUG("s_Graphics->GetRenderer() \n");

            s_DeviceType = s_Graphics->GetRenderer();

            DEBUG("CreateRenderAPI(s_DeviceType) \n");
            DEBUG("s_DeviceType = %s \n", GetRendererName(s_DeviceType));

            EarlyRenderAPI = CreateRenderAPI(s_DeviceType);
            return;
        }
    }

    if(EarlyRenderAPI){
        EarlyRenderAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
    } else {
        DEBUG("Unable to process event, no Render API");
    }

    // Let the implementation process the device related events
    std::map<libvlc_media_player_t*, RenderAPI*>::iterator it;

    for(it = contexts.begin(); it != contexts.end(); it++)
    {
        RenderAPI* currentAPI = it->second;
        if(currentAPI) {
            DEBUG(" currentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces); \n");
            currentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces);
        }
    }
    for (auto retired = retiredContexts.begin();
         retired != retiredContexts.end();) {
        RenderAPI* api = *retired;
        api->ProcessDeviceEvent(eventType, s_UnityInterfaces);
        if (eventType == kUnityGfxDeviceEventShutdown && api->canDestroy()) {
            delete api;
            retired = retiredContexts.erase(retired);
        } else {
            ++retired;
        }
    }
}

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
    std::lock_guard<std::mutex> lock(s_contextsMutex);
#if !defined(_WIN32)
    DEBUG_VERBOSE("[VLC-Unity] OnRenderEvent called with eventID=%d, thread=%ld\n", eventID, (long)pthread_self());
#else
    DEBUG_VERBOSE("[VLC-Unity] OnRenderEvent called with eventID=%d\n", eventID);
#endif
    DEBUG_VERBOSE("[VLC-Unity]   s_DeviceType=%s\n", GetRendererName(s_DeviceType));
    DEBUG_VERBOSE("[VLC-Unity]   contexts.size()=%zu\n", contexts.size());

    if (eventID == kRenderThreadWorkEvent && EarlyRenderAPI)
    {
        DEBUG_VERBOSE("[VLC-Unity]   Calling EarlyRenderAPI->retrieveOpenGLContext()\n");
        EarlyRenderAPI->retrieveOpenGLContext();
    }

#if defined(SHOW_WATERMARK)
    if (trial_is_expired() && !g_trialIsStopped.load())
    {
        DEBUG("[Trial] stopping media players after trial expiry");
        g_trialIsStopped.store(true);
        g_trialIsPaused.store(false);
        trial_pause();

        std::map<libvlc_media_player_t*, RenderAPI*>::iterator it;
        for(it = contexts.begin(); it != contexts.end(); it++)
        {
            libvlc_media_player_t* mp = it->first;
            if(mp && libvlc_media_player_is_playing(mp))
                libvlc_media_player_stop_async(mp);
        }
    }
#endif

    if (eventID == kRenderThreadWorkEvent) {
        std::map<libvlc_media_player_t*, RenderAPI*>::iterator it;
        for(it = contexts.begin(); it != contexts.end(); it++)
        {
            RenderAPI* currentAPI = it->second;
            if(currentAPI && !currentAPI->isInitialized())
                currentAPI->ProcessDeviceEvent(kUnityGfxDeviceEventInitialize, s_UnityInterfaces);
        }
    }

    for (auto& context : contexts) {
        RenderAPI* renderer = context.second;
        if (!renderer)
            continue;
        if (eventID == kVulkanQueueSubmissionEvent)
            renderer->performQueueSubmissionWork();
        else if (eventID == kVulkanCopyEvent ||
                 eventID == kRenderThreadWorkEvent)
            renderer->performRenderThreadWork();
    }

    for (auto retired = retiredContexts.begin();
         retired != retiredContexts.end();) {
        RenderAPI* api = *retired;
        if (eventID == kVulkanQueueSubmissionEvent)
            api->performQueueSubmissionWork();
        else
            api->performRenderThreadWork();
        if (api->canDestroy()) {
            delete api;
            retired = retiredContexts.erase(retired);
        } else {
            ++retired;
        }
    }
}

#if defined(SHOW_WATERMARK)
static int64_t getCurrentTimeMs()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

static void trial_reset()
{
    g_trialAccumulatedMs.store(0);
    g_trialLastTickMs.store(-1);
}

static void trial_pause()
{
    g_trialLastTickMs.store(-1);
}

static bool trial_is_expired()
{
    return g_trialAccumulatedMs.load() >= TRIAL_TIME_LIMIT_MS;
}

extern "C" bool libvlc_unity_trial_tick()
{
    int64_t nowMs = getCurrentTimeMs();
    int64_t lastTick = g_trialLastTickMs.load();

    if (lastTick >= 0)
    {
        int64_t delta = nowMs - lastTick;
        g_trialAccumulatedMs.fetch_add(delta);
    }
    g_trialLastTickMs.store(nowMs);

    int64_t accumulated = g_trialAccumulatedMs.load();
    bool stillValid = accumulated < TRIAL_TIME_LIMIT_MS;
    if (!stillValid)
    {
        DEBUG("[Trial] trial_tick: EXPIRED (accumulated=%lldms, limit=%lldms)", (long long)accumulated, (long long)TRIAL_TIME_LIMIT_MS);
    }
    return stillValid;
}

extern "C" uint32_t libvlc_unity_trial_seconds_remaining()
{
    int64_t accumulated = g_trialAccumulatedMs.load();
    if (accumulated >= TRIAL_TIME_LIMIT_MS)
        return 0;

    return (uint32_t)((TRIAL_TIME_LIMIT_MS - accumulated) / 1000);
}

extern "C" bool libvlc_unity_trial_is_paused()
{
    bool val = g_trialIsPaused.load();
    return val;
}

extern "C" bool libvlc_unity_trial_is_stopped()
{
    bool val = g_trialIsStopped.load();
    return val;
}
#endif // SHOW_WATERMARK

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_is_trial()
{
#if defined(SHOW_WATERMARK)
    return true;
#else
    return false;
#endif
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
    DEBUG_VERBOSE("[VLC-Unity] GetRenderEventFunc called, returning %p\n", (void*)OnRenderEvent);
    return OnRenderEvent;
}
