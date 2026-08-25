#pragma once

#include "VulkanFrameSource.h"
#include <cstddef>

enum class VulkanSubmissionCompletionStatus
{
    Complete,
    Failed,
};

struct VulkanSubmissionCompletion
{
    uint64_t token = 0;
    VulkanSubmissionCompletionStatus status = VulkanSubmissionCompletionStatus::Failed;
};

class IVulkanSubmissionStrategy
{
public:
    virtual ~IVulkanSubmissionStrategy() = default;

    virtual bool begin(const VulkanExternalFrame& frame, VkCommandBuffer& commandBuffer) = 0;
    virtual bool endAndSubmit(const VulkanExternalFrame& frame) = 0;
    virtual void cancel(const VulkanExternalFrame& frame) = 0;
    virtual size_t collectCompletions(VulkanSubmissionCompletion* completions,
                                      size_t capacity) = 0;
    virtual bool completesImmediately() const = 0;
    virtual bool hasInFlightWork() const = 0;
    virtual void shutdown() = 0;
};
