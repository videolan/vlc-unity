#include "RenderAPI_VulkanLinux.h"
#include "Log.h"

RenderAPI* CreateRenderAPI_VulkanLinux(UnityGfxRenderer apiType)
{
    return new RenderAPI_VulkanLinux(apiType);
}

RenderAPI_VulkanLinux::RenderAPI_VulkanLinux(UnityGfxRenderer apiType)
{
    (void)apiType;
}

RenderAPI_VulkanLinux::~RenderAPI_VulkanLinux()
{
    shutdown();
}

void RenderAPI_VulkanLinux::ProcessDeviceEvent(
    UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
    if (type == kUnityGfxDeviceEventInitialize) {
        if (m_initialized || m_initializationAttempted)
            return;
        m_initializationAttempted = true;
        m_stopping.store(false);
        m_graphics = interfaces ? interfaces->Get<IUnityGraphicsVulkan>() : nullptr;
        if (!m_graphics) {
            DEBUG("[Vulkan-Linux] IUnityGraphicsVulkan is unavailable. Select OpenGLCore.");
            return;
        }
        m_instance = m_graphics->Instance();
        if (!m_frameSource.initialize(m_graphics, m_instance) ||
            !m_submission.initialize(m_graphics, m_instance)) {
            DEBUG("[Vulkan-Linux] external-frame or queue initialization failed. Select OpenGLCore.");
            shutdown();
            return;
        }
        VulkanCopyCommandFunctions functions;
        PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
        if (m_instance.getInstanceProcAddr) {
            getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                m_instance.getInstanceProcAddr(
                    m_instance.instance, "vkGetDeviceProcAddr"));
        }
        if (getDeviceProcAddr) {
            functions.cmdPipelineBarrier =
                reinterpret_cast<PFN_vkCmdPipelineBarrier>(
                    getDeviceProcAddr(
                        m_instance.device, "vkCmdPipelineBarrier"));
            functions.cmdCopyImage = reinterpret_cast<PFN_vkCmdCopyImage>(
                getDeviceProcAddr(m_instance.device, "vkCmdCopyImage"));
        }
        if (!m_copyCore.initialize(
                m_graphics, &m_frameSource, &m_submission, functions)) {
            shutdown();
            return;
        }
        m_initialized = true;
        if (m_mediaPlayer && !m_frameSource.setVlcContext(m_mediaPlayer)) {
            DEBUG("[Vulkan-Linux] VLC producer callbacks could not be restored after device initialization");
            shutdown();
            return;
        }
        DEBUG("[Vulkan-Linux] renderer initialized with plugin-owned queue submission");
    } else if (type == kUnityGfxDeviceEventShutdown) {
        m_stopping.store(true);
        if (m_mediaPlayer)
            m_frameSource.unsetVlcContext(m_mediaPlayer);
        performRenderThreadWork();
        if (m_frameSource.hasPendingFrames() || m_copyCore.hasInFlightWork()) {
            DEBUG("[Vulkan-Linux] device shutdown found unfinished nonblocking work; Vulkan handles are delegated to VkDevice teardown");
            m_submission.abandonForDeviceShutdown();
            m_copyCore.shutdown();
            m_frameSource.abandonForDeviceShutdown();
            m_graphics = nullptr;
            m_instance = {};
            m_initialized = false;
        } else {
            shutdown();
        }
        if (!m_initialized)
            m_initializationAttempted = false;
    }
}

void RenderAPI_VulkanLinux::setVlcContext(libvlc_media_player_t* mediaPlayer)
{
    m_mediaPlayer = mediaPlayer;
    if (!m_initialized || !m_frameSource.setVlcContext(mediaPlayer))
        DEBUG("[Vulkan-Linux] VLC producer callbacks were not registered");
}

void RenderAPI_VulkanLinux::unsetVlcContext(libvlc_media_player_t* mediaPlayer)
{
    m_frameSource.unsetVlcContext(mediaPlayer);
    if (m_mediaPlayer == mediaPlayer)
        m_mediaPlayer = nullptr;
}

void* RenderAPI_VulkanLinux::getVideoFrame(
    unsigned width, unsigned height, bool* outUpdated)
{
    (void)width;
    (void)height;
    const bool updated = m_initialized && !m_stopping.load() &&
                         m_frameSource.consumeFrameNotification();
    if (outUpdated)
        *outUpdated = updated;
    return updated ? reinterpret_cast<void*>(static_cast<uintptr_t>(1)) : nullptr;
}

void RenderAPI_VulkanLinux::performRenderThreadWork()
{
    if (!m_initialized)
        return;
    if (!m_stopping.load())
        m_copyCore.performRenderThreadWork();
    else {
        m_frameSource.discardPendingFrames();
        m_copyCore.pollCompletions();
    }
    if (m_frameSource.failed() && !m_stopping.exchange(true)) {
        DEBUG("[Vulkan-Linux] disabling failed backend; select OpenGLCore");
    }
}

void RenderAPI_VulkanLinux::performQueueSubmissionWork()
{
    if (m_initialized)
        m_submission.submitPendingQueueWork();
}

bool RenderAPI_VulkanLinux::setUnityTexture(void* unityTexture)
{
    return m_initialized && m_copyCore.setUnityTexture(unityTexture);
}

bool RenderAPI_VulkanLinux::canDestroy() const
{
    return !m_initialized ||
        (!m_frameSource.hasPendingFrames() && !m_copyCore.hasInFlightWork());
}

void RenderAPI_VulkanLinux::prepareForPluginUnload()
{
    m_stopping.store(true);
    if (!m_initialized && !m_graphics)
        return;

    if (m_frameSource.hasPendingFrames() || m_copyCore.hasInFlightWork()) {
        // There will be no later render event after the module is unloaded.
        // Invalidate queued callback owners and delegate still-live Vulkan
        // handles to the imminent VkDevice teardown without a CPU wait.
        m_submission.abandonForDeviceShutdown();
        m_copyCore.shutdown();
        m_frameSource.abandonForDeviceShutdown();
        m_graphics = nullptr;
        m_instance = {};
        m_initialized = false;
        m_initializationAttempted = false;
        return;
    }
    shutdown();
    m_initializationAttempted = false;
}

void RenderAPI_VulkanLinux::shutdown()
{
    if (!m_initialized && !m_graphics)
        return;
    if (m_frameSource.hasPendingFrames() || m_copyCore.hasInFlightWork()) {
        // Player destruction is deferred by RenderingPlugin until nonblocking
        // fence polling reports completion.
        return;
    }
    m_copyCore.shutdown();
    m_submission.shutdown();
    m_frameSource.shutdown();
    m_graphics = nullptr;
    m_instance = {};
    m_initialized = false;
}
