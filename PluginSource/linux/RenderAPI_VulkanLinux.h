#pragma once

#include "LinuxDMABufFrameSource.h"
#include "vulkan/OwnedQueueSubmissionStrategy.h"
#include "RenderAPI.h"
#include "vulkan/VulkanUnityCopyCore.h"

#include <atomic>

class RenderAPI_VulkanLinux final : public RenderAPI
{
public:
    explicit RenderAPI_VulkanLinux(UnityGfxRenderer apiType);
    ~RenderAPI_VulkanLinux() override;

    void ProcessDeviceEvent(UnityGfxDeviceEventType type,
                            IUnityInterfaces* interfaces) override;
    void setVlcContext(libvlc_media_player_t* mediaPlayer) override;
    void unsetVlcContext(libvlc_media_player_t* mediaPlayer) override;
    void* getVideoFrame(unsigned width, unsigned height,
                        bool* outUpdated) override;
    void performRenderThreadWork() override;
    void performQueueSubmissionWork() override;
    bool setUnityTexture(void* unityTexture) override;
    // A failed capability/bootstrap attempt is terminal for this Unity device;
    // returning true here prevents per-frame retries and diagnostic spam.
    bool isInitialized() const override
    {
        return m_initialized || m_initializationAttempted;
    }
    void beginShutdown() override { m_stopping.store(true); }
    void prepareForPluginUnload() override;
    bool canDestroy() const override;

private:
    void shutdown();

    UnityVulkanInstance m_instance = {};
    IUnityGraphicsVulkan* m_graphics = nullptr;
    LinuxDMABufFrameSource m_frameSource;
    OwnedQueueSubmissionStrategy m_submission;
    VulkanUnityCopyCore m_copyCore;
    libvlc_media_player_t* m_mediaPlayer = nullptr;
    std::atomic<bool> m_stopping { false };
    bool m_initializationAttempted = false;
    bool m_initialized = false;
};
