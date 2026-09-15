#include "PlatformBase.h"
#include "RenderAPI.h"
#include "RenderAPIRegistry.h"
#include "Log.h"
#include "TrialWatermark.h"

#include <atomic>
#include <chrono>
#include <memory>
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

static std::atomic<UnityGfxRenderer> s_DeviceType { kUnityGfxRendererNull };

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
static RenderAPIRegistry s_contexts;
static RenderAPIEntryPtr s_earlyRenderAPI;
static std::mutex s_earlyRenderAPIMutex;
static IUnityInterfaces* s_UnityInterfaces = NULL;

static std::atomic<int> s_color_space { 0 };

enum RenderEventId
{
    kVulkanCopyEvent = 0,
    kRenderThreadWorkEvent = 1,
    kVulkanQueueSubmissionEvent = 2,
    kRendererCleanupEvent = 3,
};

static RenderAPIEntryPtr earlyRenderAPISnapshot()
{
    std::lock_guard<std::mutex> lock(s_earlyRenderAPIMutex);
    return s_earlyRenderAPI;
}

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
    s_color_space.store(color_space);
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

    const RenderAPIEntryPtr context = s_contexts.findActive(mp);
    if (!context)
        return;
    std::lock_guard<std::mutex> lock(context->callMutex);
    if (context->state != RenderAPIEntryState::Active || !context->renderer)
        return;
    context->renderer->setbitDepthFormat(bit_depth);
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

    libvlc_media_player_t* mp = nullptr;
    std::unique_ptr<RenderAPI> currentAPI;
    bool vlcContextSet = false;
    UnityGfxRenderer deviceType = kUnityGfxRendererNull;

    const struct libvlc_media_player_cbs* effective_callbacks = callbacks;
#if defined(SHOW_WATERMARK)
    effective_callbacks = callbacks_with_trial_state(callbacks);
#endif
    mp = libvlc_media_player_new(inst, effective_callbacks, callbacks_opaque);

    if (mp == NULL) {
        DEBUG("Error initializing media player");
        goto err;
    }

    DEBUG("Calling... Initialize Render API \n");
    if (!s_Graphics) {
        DEBUG("Unity graphics interface is unavailable");
        goto err;
    }
    deviceType = s_Graphics->GetRenderer();
    s_DeviceType.store(deviceType);
    if(deviceType == kUnityGfxRendererNull)
    {
        DEBUG("s_DeviceType is NULL \n");
        goto err;
    }

    DEBUG("Calling... CreateRenderAPI \n");
    DEBUG("s_DeviceType = %s \n", GetRendererName(deviceType));

    currentAPI.reset(CreateRenderAPI(deviceType));

    if(!currentAPI)
    {
        DEBUG("s_CurrentAPI is NULL \n");
        goto err;
    }

    DEBUG("Calling... ProcessDeviceEvent \n");

    currentAPI->ProcessDeviceEvent(
        kUnityGfxDeviceEventInitialize, s_UnityInterfaces);
    currentAPI->setColorSpace(s_color_space.load());

    DEBUG("Calling... setVlcContext s_CurrentAPI=%p mp=%p", currentAPI.get(), mp);
    currentAPI->setVlcContext(mp);
    vlcContextSet = true;
    if (currentAPI->hasVideoOutputFailure()) {
        DEBUG("Unable to install Unity video output, refusing standalone fallback");
        goto err;
    }

    if (!s_contexts.insert(mp, std::move(currentAPI))) {
        DEBUG("Renderer registry rejected a duplicate media player");
        goto err;
    }

    return mp;
err:
    if (currentAPI && vlcContextSet)
        currentAPI->unsetVlcContext(mp);
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

    const RenderAPIEntryPtr context = s_contexts.beginRetirement(mp);
    if (!context) {
        libvlc_media_player_release(mp);
        return;
    }

    std::lock_guard<std::mutex> lock(context->callMutex);
    if (context->state == RenderAPIEntryState::Retiring && context->renderer) {
        context->renderer->beginShutdown();
        context->renderer->unsetVlcContext(mp);
    }
    libvlc_media_player_release(mp);
    context->mediaPlayer = nullptr;
    if (context->state == RenderAPIEntryState::Retiring)
        context->state = RenderAPIEntryState::Retired;
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_has_retired_renderers()
{
    return s_contexts.hasRetired();
}

extern "C" void* UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_get_texture(libvlc_media_player_t* mp, unsigned width, unsigned height, bool * updated)
{
    if (updated)
        *updated = false;

    if(mp == NULL || !updated)
        return NULL;

    if(width == 0 && height == 0)
        return NULL;

    const RenderAPIEntryPtr context = s_contexts.findActive(mp);
    if (!context)
        return nullptr;
    std::lock_guard<std::mutex> lock(context->callMutex);
    if (context->state != RenderAPIEntryState::Active || !context->renderer)
        return nullptr;

#if defined(SHOW_WATERMARK)
    bool isStopped = libvlc_unity_trial_is_stopped();
    if (!libvlc_media_player_is_playing(mp) && !isStopped)
        return NULL;
#else
    if (!libvlc_media_player_is_playing(mp))
        return NULL;
#endif

    return context->renderer->getVideoFrame(width, height, updated);
}

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
libvlc_unity_set_unity_texture_vulkan(libvlc_media_player_t* mp, void* unityTexturePtr)
{
    (void)unityTexturePtr;
    if(mp == NULL) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: mp is NULL");
        return false;
    }

    const RenderAPIEntryPtr context = s_contexts.findActive(mp);
    if (!context) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: no context found for mp");
        return false;
    }

    std::lock_guard<std::mutex> lock(context->callMutex);
    if (context->state != RenderAPIEntryState::Active || !context->renderer) {
        DEBUG("libvlc_unity_set_unity_texture_vulkan: s_CurrentAPI is NULL");
        return false;
    }

    if (s_DeviceType.load() == kUnityGfxRendererVulkan)
        return context->renderer->setUnityTexture(unityTexturePtr);

    DEBUG("libvlc_unity_set_unity_texture_vulkan: not on Vulkan renderer");
    return false;
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

#if defined(SUPPORT_VULKAN) && defined(UNITY_LINUX)
static void ConfigureVulkanQueueSubmissionEvent(UnityGfxRenderer deviceType)
{
    if (deviceType != kUnityGfxRendererVulkan || !s_UnityInterfaces)
        return;

    IUnityGraphicsVulkan* vulkan =
        s_UnityInterfaces->Get<IUnityGraphicsVulkan>();
    if (!vulkan || !vulkan->ConfigureEvent)
        return;

    UnityVulkanPluginEventConfig queueEvent = {};
    queueEvent.renderPassPrecondition = kUnityVulkanRenderPass_DontCare;
    queueEvent.graphicsQueueAccess = kUnityVulkanGraphicsQueueAccess_Allow;
    queueEvent.flags =
        kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission |
        kUnityVulkanEventConfigFlag_FlushCommandBuffers;
    vulkan->ConfigureEvent(kVulkanQueueSubmissionEvent, &queueEvent);
}
#endif

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

    std::vector<RenderAPIEntryPtr> contexts = s_contexts.takeAll();
    for (const RenderAPIEntryPtr& context : contexts) {
        std::lock_guard<std::mutex> lock(context->callMutex);
        const RenderAPIEntryState previous = context->state;
        context->state = RenderAPIEntryState::Destroying;
        if (!context->renderer)
            continue;
        if (previous == RenderAPIEntryState::Active ||
            previous == RenderAPIEntryState::Retiring) {
            context->renderer->beginShutdown();
            if (context->mediaPlayer)
                context->renderer->unsetVlcContext(context->mediaPlayer);
        }
        context->renderer->prepareForPluginUnload();
    }

    RenderAPIEntryPtr early;
    {
        std::lock_guard<std::mutex> lock(s_earlyRenderAPIMutex);
        early = std::move(s_earlyRenderAPI);
    }
    if (early) {
        std::lock_guard<std::mutex> lock(early->callMutex);
        early->state = RenderAPIEntryState::Destroying;
        if (early->renderer)
            early->renderer->prepareForPluginUnload();
    }
    s_UnityInterfaces = nullptr;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
    VLCUnity_UnityPluginUnload();
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
    // Create graphics API implementation upon initialization
    if (eventType == kUnityGfxDeviceEventInitialize) {
        const auto deviceTime = std::chrono::steady_clock::now().time_since_epoch();
        DEBUG("[Vulkan] graphics-device initialize observed at %lld us",
              static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(deviceTime).count()));
        DEBUG("Initialise Render API");
        bool created = false;
        {
            std::lock_guard<std::mutex> lock(s_earlyRenderAPIMutex);
            if (s_earlyRenderAPI) {
                DEBUG("*** Reinitialising existing EarlyRenderAPI ***");
            } else {
                DEBUG("s_Graphics->GetRenderer() \n");
                const UnityGfxRenderer deviceType = s_Graphics->GetRenderer();
                s_DeviceType.store(deviceType);

#if defined(SUPPORT_VULKAN) && defined(UNITY_LINUX)
                ConfigureVulkanQueueSubmissionEvent(deviceType);
#endif

                DEBUG("CreateRenderAPI(s_DeviceType) \n");
                DEBUG("s_DeviceType = %s \n", GetRendererName(deviceType));

                std::unique_ptr<RenderAPI> renderer(CreateRenderAPI(deviceType));
                if (renderer) {
                    s_earlyRenderAPI = std::make_shared<RenderAPIEntry>(
                        nullptr, std::move(renderer));
                    created = true;
                }
            }
        }
        if (created)
            return;
    }

    const RenderAPIEntryPtr early = earlyRenderAPISnapshot();
    if (early) {
        std::lock_guard<std::mutex> lock(early->callMutex);
        if (early->state != RenderAPIEntryState::Destroying && early->renderer)
            early->renderer->ProcessDeviceEvent(eventType, s_UnityInterfaces);
    } else {
        DEBUG("Unable to process event, no Render API");
    }

    for (const RenderAPIEntryPtr& context : s_contexts.activeSnapshot()) {
        std::lock_guard<std::mutex> lock(context->callMutex);
        if (context->state == RenderAPIEntryState::Active && context->renderer) {
            DEBUG(" currentAPI->ProcessDeviceEvent(eventType, s_UnityInterfaces); \n");
            context->renderer->ProcessDeviceEvent(eventType, s_UnityInterfaces);
        }
    }

    for (const RenderAPIEntryPtr& context : s_contexts.retiredSnapshot()) {
        bool remove = false;
        {
            std::lock_guard<std::mutex> lock(context->callMutex);
            if (context->state != RenderAPIEntryState::Retired ||
                !context->renderer) {
                continue;
            }
            context->renderer->ProcessDeviceEvent(eventType, s_UnityInterfaces);
            if (eventType == kUnityGfxDeviceEventShutdown &&
                context->renderer->canDestroy()) {
                context->state = RenderAPIEntryState::Destroying;
                remove = true;
            }
        }
        if (remove)
            s_contexts.removeRetired(context);
    }
}

static void UNITY_INTERFACE_API OnRenderEvent(int eventID)
{
#if !defined(_WIN32)
    DEBUG_VERBOSE("[VLC-Unity] OnRenderEvent called with eventID=%d, thread=%ld\n", eventID, (long)pthread_self());
#else
    DEBUG_VERBOSE("[VLC-Unity] OnRenderEvent called with eventID=%d\n", eventID);
#endif
    DEBUG_VERBOSE("[VLC-Unity]   s_DeviceType=%s\n",
                  GetRendererName(s_DeviceType.load()));
    DEBUG_VERBOSE("[VLC-Unity]   contexts.size()=%zu\n",
                  s_contexts.activeCount());

    const RenderAPIEntryPtr early = earlyRenderAPISnapshot();
    if (eventID == kRenderThreadWorkEvent && early) {
        std::lock_guard<std::mutex> lock(early->callMutex);
        DEBUG_VERBOSE("[VLC-Unity]   Calling EarlyRenderAPI->retrieveOpenGLContext()\n");
        if (early->state != RenderAPIEntryState::Destroying && early->renderer)
            early->renderer->retrieveOpenGLContext();
    }

#if defined(SHOW_WATERMARK)
    if (trial_is_expired() && !g_trialIsStopped.load())
    {
        DEBUG("[Trial] stopping media players after trial expiry");
        g_trialIsStopped.store(true);
        g_trialIsPaused.store(false);
        trial_pause();

        for (const RenderAPIEntryPtr& context : s_contexts.activeSnapshot()) {
            std::lock_guard<std::mutex> lock(context->callMutex);
            if (context->state == RenderAPIEntryState::Active &&
                context->mediaPlayer &&
                libvlc_media_player_is_playing(context->mediaPlayer)) {
                libvlc_media_player_stop_async(context->mediaPlayer);
            }
        }
    }
#endif

    for (const RenderAPIEntryPtr& context : s_contexts.activeSnapshot()) {
        std::lock_guard<std::mutex> lock(context->callMutex);
        if (context->state != RenderAPIEntryState::Active || !context->renderer)
            continue;
        if (eventID == kRenderThreadWorkEvent &&
            !context->renderer->isInitialized()) {
            context->renderer->ProcessDeviceEvent(
                kUnityGfxDeviceEventInitialize, s_UnityInterfaces);
        }
        if (eventID == kVulkanQueueSubmissionEvent)
            context->renderer->performQueueSubmissionWork();
        else if (eventID == kVulkanCopyEvent ||
                 eventID == kRenderThreadWorkEvent)
            context->renderer->performRenderThreadWork();
    }

    for (const RenderAPIEntryPtr& context : s_contexts.retiredSnapshot()) {
        bool remove = false;
        {
            std::lock_guard<std::mutex> lock(context->callMutex);
            if (context->state != RenderAPIEntryState::Retired ||
                !context->renderer) {
                continue;
            }
            if (eventID == kVulkanQueueSubmissionEvent) {
                context->renderer->performQueueSubmissionWork();
                continue;
            }
            if (eventID != kRendererCleanupEvent)
                continue;
            if (!context->observeCleanupEvent())
                continue;
            context->renderer->performRenderThreadWork();
            if (context->renderer->canDestroy()) {
                context->state = RenderAPIEntryState::Destroying;
                remove = true;
            }
        }
        if (remove)
            s_contexts.removeRetired(context);
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
