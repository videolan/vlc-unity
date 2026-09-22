#pragma once

#include "LinuxDMABufProducer.h"
#include "LinuxVulkanDeviceIdentity.h"
#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityGraphicsVulkan.h"
#include "vulkan/VulkanFrameSource.h"

#if defined(SHOW_WATERMARK)
#include "RenderAPI_OpenGLWatermark.h"
#endif

#include <EGL/egl.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class LinuxDMABufFrameSource final : public IVulkanFrameSource,
                                    public ILinuxDMABufProducerContext,
                                    public ILinuxDMABufProducerObserver,
                                    public ILinuxVulkanSlotObserver
{
public:
    LinuxDMABufFrameSource();
    ~LinuxDMABufFrameSource() override;

    bool initialize(IUnityGraphicsVulkan* graphics,
                    const UnityVulkanInstance& instance);
    void shutdown();
    void abandonForDeviceShutdown();
    bool setVlcContext(libvlc_media_player_t* mediaPlayer);
    void unsetVlcContext(libvlc_media_player_t* mediaPlayer);
    bool initialized() const { return m_initialized; }
    bool failed() const { return m_failed.load(); }
    bool consumeFrameNotification();
    bool hasPendingFrames() const { return m_pendingFrameCount.load() != 0; }
    void discardPendingFrames();

    bool acquireReadyFrame(VulkanExternalFrame& frame) override;
    void onCopyDeferred(uint64_t token) override;
    bool onCopyScheduled(uint64_t token) override;
    void onCopyComplete(uint64_t token) override;
    void onCopyFailed(uint64_t token) override;

    bool producerMakeCurrent(bool current) override;
    void* producerLoadProc(const char* name) override;
    EGLDisplay producerEGLDisplay() const override { return m_display; }
    bool onProducerSetup() override;
    void onProducerCleanup() override;
    bool onBeforeProducerSwap(GLuint framebuffer,
                              unsigned width, unsigned height) override;
    bool onDMABufSlotCreated(size_t index,
                            const LinuxDMABufSlot& slot) override;
    void onDMABufSlotDestroying(size_t index,
                                const LinuxDMABufSlot& slot) override;
    bool beginDMABufSlotDestruction() override;
    void endDMABufSlotDestruction() override;
    bool beginVulkanRendering(size_t index,
                              const LinuxDMABufSlot& slot) override;
    bool advanceVulkanSlots(size_t rendered,
                            const LinuxDMABufSlot& renderedSlot,
                            size_t& next) override;

private:
    enum class SlotState
    {
        AvailableToGL,
        RenderingInGL,
        ReadyForCopy,
        CopyInFlight,
        Failed,
    };

    struct VulkanSlot
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkSemaphore sourceReady = VK_NULL_HANDLE;
        VkSemaphore copyComplete = VK_NULL_HANDLE;
        GLuint glSourceReady = 0;
        GLuint glCopyComplete = 0;
        VkExtent3D extent = { 0, 0, 0 };
        uint64_t token = 0;
    };

    struct RetiredSlot
    {
        VulkanSlot resources;
        uint64_t readySequence = 0;
        bool ready = false;
        bool inFlight = false;
    };

    bool queryAndOpenExactDevice();
    bool loadVulkanFunctions();
    bool validateExternalSemaphoreSupport();
    bool createProducerContext();
    bool validateUuidCompatibility();
    bool loadInteropFunctions();
    bool importImage(size_t index, const LinuxDMABufSlot& slot);
    bool createSemaphorePair(size_t index);
    bool createSharedSemaphore(VkSemaphore& semaphore, GLuint& glSemaphore);
    bool createSlotVulkanResources(size_t index, const LinuxDMABufSlot& source);
    void destroyGlSemaphores(VulkanSlot& slot);
    void destroyVulkanSlot(size_t index, bool haveGlContext);
    void destroyVulkanResources(VulkanSlot& slot, bool haveGlContext);
    uint32_t chooseMemoryType(uint32_t bits) const;
    void applyPendingStateLocked();
    bool completeTokenLocked(uint64_t token);
    bool reserveToken(uint64_t token);
    bool releaseReservedToken(uint64_t token);
    void restoreFrameNotification();
    bool deferCompletion(uint64_t token);
    void shutdownInternal(bool abandonVulkanHandles);

    UnityVulkanInstance m_instance = {};
    LinuxGBMDevice m_gbm;
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLContext m_context = EGL_NO_CONTEXT;
    std::unique_ptr<LinuxDMABufProducer> m_producer;
    std::array<VulkanSlot, LinuxDMABufProducer::SlotCount> m_slots;
    std::vector<RetiredSlot> m_retiredSlots;
    std::array<SlotState, LinuxDMABufProducer::SlotCount> m_slotStates = {{
        SlotState::RenderingInGL,
        SlotState::AvailableToGL,
        SlotState::AvailableToGL,
    }};
    std::array<bool, LinuxDMABufProducer::SlotCount> m_recycledSlots = {{
        false, false, false
    }};
    std::mutex m_stateMutex;
    std::atomic<bool> m_failed { false };
    std::atomic<uint32_t> m_frameNotifications { 0 };
    std::atomic<uint32_t> m_pendingFrameCount { 0 };
    std::array<std::atomic<uint64_t>, LinuxDMABufProducer::SlotCount> m_reservedTokens;
    std::array<std::atomic<uint64_t>, LinuxDMABufProducer::SlotCount> m_pendingCompletionTokens;
    std::array<uint64_t, LinuxDMABufProducer::SlotCount> m_readySequence = {{ 0, 0, 0 }};
    uint64_t m_nextReadySequence = 0;
    std::atomic<uint64_t> m_nextToken { 0 };
    std::atomic<bool> m_slotsChanging { false };
    bool m_forceDestroy = false;
    bool m_initialized = false;
    LinuxGraphicsDeviceUuids m_vulkanUuids;

    PFN_vkGetDeviceProcAddr m_getDeviceProcAddr = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties m_enumerateDeviceExtensionProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 m_getPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties2 m_getPhysicalDeviceFormatProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 m_getPhysicalDeviceImageFormatProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties m_getPhysicalDeviceExternalSemaphoreProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties m_getPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkCreateImage m_createImage = nullptr;
    PFN_vkDestroyImage m_destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements m_getImageMemoryRequirements = nullptr;
    PFN_vkAllocateMemory m_allocateMemory = nullptr;
    PFN_vkFreeMemory m_freeMemory = nullptr;
    PFN_vkBindImageMemory m_bindImageMemory = nullptr;
    PFN_vkCreateSemaphore m_createSemaphore = nullptr;
    PFN_vkDestroySemaphore m_destroySemaphore = nullptr;
    PFN_vkGetMemoryFdPropertiesKHR m_getMemoryFdProperties = nullptr;
    PFN_vkGetSemaphoreFdKHR m_getSemaphoreFd = nullptr;
    PFNGLGENSEMAPHORESEXTPROC m_glGenSemaphores = nullptr;
    PFNGLDELETESEMAPHORESEXTPROC m_glDeleteSemaphores = nullptr;
    PFNGLIMPORTSEMAPHOREFDEXTPROC m_glImportSemaphoreFd = nullptr;
    PFNGLWAITSEMAPHOREEXTPROC m_glWaitSemaphore = nullptr;
    PFNGLSIGNALSEMAPHOREEXTPROC m_glSignalSemaphore = nullptr;
#if defined(SHOW_WATERMARK)
    OpenGLWatermark m_watermark;
#endif
};
