#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class LinuxOpenGLBackend
{
    Unknown,
    GLX,
    EGL,
};

struct LinuxDrmRenderNode
{
    std::string path;
    uint32_t deviceMajor = 0;
    uint32_t deviceMinor = 0;
};

enum class LinuxDrmMatchError
{
    ExactMatch,
    IdentityUnavailable,
    NoCandidates,
    InvalidNode,
    NoExactMatch,
    DuplicateExactMatch,
    OverrideMismatch,
};

struct LinuxDrmMatchResult
{
    LinuxDrmMatchError error = LinuxDrmMatchError::NoCandidates;
    std::string path;
    std::string diagnostic;
    std::vector<LinuxDrmRenderNode> inspectedNodes;

    explicit operator bool() const { return error == LinuxDrmMatchError::ExactMatch; }
};

// Select the backend used to interoperate with Unity's OpenGL context.
// Until Unity's render-thread binding is captured, automatic selection must
// remain unknown. Display environment variables and GLES do not identify it.
// The explicit override accepts "glx" or "egl".
LinuxOpenGLBackend LinuxChooseOpenGLBackend(const char* overrideValue,
                                            LinuxOpenGLBackend unityBinding);

const char* LinuxOpenGLBackendName(LinuxOpenGLBackend backend);
bool LinuxIsOpenGLBackendOverrideValid(const char* overrideValue);
bool LinuxEnvironmentFlagEnabled(const char* value);

// Discover render nodes in deterministic minor-number order. Keeping this
// policy independent from OpenGL/Vulkan makes it reusable by future Linux
// graphics backends and architectures.
std::vector<std::string> LinuxEnumerateDrmRenderNodes(const std::string& driDirectory);

// An explicit device is exclusive: a failed override must be visible instead
// of silently falling back to another GPU.
std::vector<std::string> LinuxBuildDrmDeviceCandidates(const std::string& driDirectory,
                                                       const char* overridePath);

// Return the first candidate accepted by the backend-specific compatibility
// probe, or an empty string if none is compatible.
std::string LinuxSelectCompatibleDrmDevice(
    const std::vector<std::string>& candidates,
    const std::function<bool(const std::string&)>& probe);

bool LinuxInspectDrmRenderNode(const std::string& path,
                               LinuxDrmRenderNode& node,
                               std::string& diagnostic);

LinuxDrmMatchResult LinuxMatchDrmRenderNode(
    const std::vector<LinuxDrmRenderNode>& candidates,
    uint32_t expectedMajor,
    uint32_t expectedMinor);

LinuxDrmMatchResult LinuxResolveVulkanDrmRenderNode(
    const std::string& driDirectory,
    const char* overridePath,
    bool hasRenderIdentity,
    uint32_t expectedMajor,
    uint32_t expectedMinor);

const char* LinuxDrmMatchErrorName(LinuxDrmMatchError error);
