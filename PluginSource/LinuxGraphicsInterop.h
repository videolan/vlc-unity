#ifndef VLC_UNITY_LINUX_GRAPHICS_INTEROP_H
#define VLC_UNITY_LINUX_GRAPHICS_INTEROP_H

#include <functional>
#include <string>
#include <vector>

enum class LinuxOpenGLBackend
{
    GLX,
    EGL,
};

// Select the backend used to interoperate with Unity's OpenGL context.
// Unity normally uses GLX when DISPLAY is present, including under XWayland.
// The explicit override accepts "glx" or "egl".
LinuxOpenGLBackend LinuxChooseOpenGLBackend(const char* overrideValue,
                                            const char* x11Display,
                                            const char* waylandDisplay);

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

#endif /* VLC_UNITY_LINUX_GRAPHICS_INTEROP_H */
