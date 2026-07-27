#include "../LinuxGraphicsInterop.h"

#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void touch(const std::string& path)
{
    const int fd = open(path.c_str(), O_CREAT | O_WRONLY, 0600);
    check(fd >= 0, ("create fixture " + path).c_str());
    if (fd >= 0)
        close(fd);
}

void testBackendSelection()
{
    check(LinuxChooseOpenGLBackend(nullptr, ":0", "wayland-0") == LinuxOpenGLBackend::GLX,
          "XWayland must prefer GLX when DISPLAY is present");
    check(LinuxChooseOpenGLBackend(nullptr, nullptr, "wayland-0") == LinuxOpenGLBackend::EGL,
          "native Wayland must use EGL");
    check(LinuxChooseOpenGLBackend("egl", ":0", "wayland-0") == LinuxOpenGLBackend::EGL,
          "explicit EGL override must win");
    check(LinuxChooseOpenGLBackend("GLX", nullptr, "wayland-0") == LinuxOpenGLBackend::GLX,
          "backend override must be case insensitive");
    check(LinuxChooseOpenGLBackend("invalid", ":0", "wayland-0") == LinuxOpenGLBackend::GLX,
          "invalid override must fall back to automatic selection");
    check(LinuxIsOpenGLBackendOverrideValid(nullptr),
          "missing backend override must be valid");
    check(LinuxIsOpenGLBackendOverrideValid("egl"),
          "EGL backend override must be valid");
    check(LinuxIsOpenGLBackendOverrideValid("GLX"),
          "GLX backend override must be case insensitive");
    check(!LinuxIsOpenGLBackendOverrideValid("vulkan"),
          "unknown OpenGL backend override must be rejected");
    check(LinuxEnvironmentFlagEnabled("1"), "numeric environment flag must be enabled");
    check(LinuxEnvironmentFlagEnabled("TRUE"), "environment flag must be case insensitive");
    check(LinuxEnvironmentFlagEnabled("yes"), "yes environment flag must be enabled");
    check(!LinuxEnvironmentFlagEnabled("0"), "zero environment flag must be disabled");
    check(!LinuxEnvironmentFlagEnabled(nullptr), "missing environment flag must be disabled");
}

void testRenderNodeEnumeration()
{
    char directoryTemplate[] = "/tmp/vlc-unity-dri-XXXXXX";
    char* directory = mkdtemp(directoryTemplate);
    check(directory != nullptr, "create temporary DRI directory");
    if (!directory)
        return;

    const std::string root(directory);
    touch(root + "/renderD130");
    touch(root + "/renderD9");
    touch(root + "/renderD128");
    touch(root + "/renderD0129");
    touch(root + "/renderD");
    touch(root + "/renderD12x");
    touch(root + "/card0");

    const std::vector<std::string> nodes = LinuxEnumerateDrmRenderNodes(root);
    check(nodes.size() == 4, "enumeration must only include renderD<number> entries");
    if (nodes.size() == 4) {
        check(nodes[0] == root + "/renderD9", "render nodes must be numerically sorted (9)");
        check(nodes[1] == root + "/renderD128", "render nodes must be numerically sorted (128)");
        check(nodes[2] == root + "/renderD0129", "render nodes must be numerically sorted (129)");
        check(nodes[3] == root + "/renderD130", "render nodes must be numerically sorted (130)");
    }

    unlink((root + "/renderD130").c_str());
    unlink((root + "/renderD9").c_str());
    unlink((root + "/renderD128").c_str());
    unlink((root + "/renderD0129").c_str());
    unlink((root + "/renderD").c_str());
    unlink((root + "/renderD12x").c_str());
    unlink((root + "/card0").c_str());
    rmdir(root.c_str());
}

void testOverrideAndCompatibilityProbe()
{
    const std::vector<std::string> overridden =
        LinuxBuildDrmDeviceCandidates("/dev/dri", "/dev/dri/by-path/pci-test-render");
    check(overridden.size() == 1, "device override must be exclusive");
    check(overridden[0] == "/dev/dri/by-path/pci-test-render",
          "device override must preserve by-path symlinks");

    const std::vector<std::string> candidates = {
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
    };
    std::vector<std::string> probed;
    const std::string selected = LinuxSelectCompatibleDrmDevice(
        candidates,
        [&](const std::string& path) {
            probed.push_back(path);
            return path == "/dev/dri/renderD129";
        });

    check(selected == "/dev/dri/renderD129", "selector must return the first compatible GPU");
    check(probed.size() == 2, "selector must continue after an incompatible GPU");

    const std::string none = LinuxSelectCompatibleDrmDevice(
        candidates, [](const std::string&) { return false; });
    check(none.empty(), "selector must report when no GPU is compatible");
}

} // namespace

int main()
{
    testBackendSelection();
    testRenderNodeEnumeration();
    testOverrideAndCompatibilityProbe();

    if (failures != 0)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
