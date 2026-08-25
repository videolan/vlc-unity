#pragma once

#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityGraphicsVulkan.h"
#include "VulkanFrameSource.h"
#include "VulkanSubmissionStrategy.h"

struct VulkanCopyCommandFunctions
{
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyImage cmdCopyImage = nullptr;
};

extern const VkFormat kVulkanFrameFormat;

// Index of the first set bit in typeBits, or UINT32_MAX when empty.
inline uint32_t VulkanFirstMemoryTypeBit(uint32_t typeBits)
{
    for (uint32_t i = 0; i < 32; ++i) {
        if ((typeBits & (uint32_t(1) << i)) != 0)
            return i;
    }
    return UINT32_MAX;
}

// Shared VkImageCreateInfo for imported external frame sources.
inline void VulkanInitExternalImageInfo(
    VkImageCreateInfo& info, uint32_t width, uint32_t height,
    VkImageTiling tiling, VkImageUsageFlags usage, void* pNext)
{
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = pNext;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kVulkanFrameFormat;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = tiling;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

class VulkanUnityCopyCore
{
public:
    VulkanUnityCopyCore() = default;

    bool initialize(IUnityGraphicsVulkan* graphics,
                    IVulkanFrameSource* frameSource,
                    IVulkanSubmissionStrategy* submission,
                    const VulkanCopyCommandFunctions& functions);
    void shutdown();

    bool initialized() const
    {
        return m_graphics && m_frameSource && m_submission &&
               m_functions.cmdPipelineBarrier && m_functions.cmdCopyImage;
    }
    bool setUnityTexture(void* unityTexture);
    void performRenderThreadWork();
    void pollCompletions();
    bool hasInFlightWork() const;

    static bool RecordCopy(VkCommandBuffer commandBuffer,
                           const VulkanExternalFrame& source,
                           const UnityVulkanImage& destination,
                           const VulkanCopyCommandFunctions& functions);

private:
    IUnityGraphicsVulkan* m_graphics = nullptr;
    IVulkanFrameSource* m_frameSource = nullptr;
    IVulkanSubmissionStrategy* m_submission = nullptr;
    VulkanCopyCommandFunctions m_functions = {};
    void* m_unityTexture = nullptr;
};
