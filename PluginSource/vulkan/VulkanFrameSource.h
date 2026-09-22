#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

struct VulkanExternalFrame
{
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent = { 0, 0, 0 };
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout releaseLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags sourceAccess = 0;
    uint32_t sourceQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t copyQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t releaseQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    VkSemaphore sourceReady = VK_NULL_HANDLE;
    VkSemaphore copyComplete = VK_NULL_HANDLE;
    uint64_t token = 0;
    bool acquireOwnership = false;
    bool releaseOwnership = false;
};

class IVulkanFrameSource
{
public:
    virtual ~IVulkanFrameSource() = default;

    // Nonblocking. A false result means that no frame is currently ready.
    virtual bool acquireReadyFrame(VulkanExternalFrame& frame) = 0;
    virtual void onCopyDeferred(uint64_t token) = 0;
    // Nonblocking. False asks the core to cancel and retry the frame later.
    virtual bool onCopyScheduled(uint64_t token) = 0;
    virtual void onCopyComplete(uint64_t token) = 0;
    virtual void onCopyFailed(uint64_t token) = 0;
};
