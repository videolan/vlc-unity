#include "../linux/LinuxGraphicsInterop.h"

#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/sysmacros.h>
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
    check(LinuxChooseOpenGLBackend(nullptr, LinuxOpenGLBackend::Unknown) == LinuxOpenGLBackend::Unknown,
          "automatic selection must wait for a captured Unity context");
    check(LinuxChooseOpenGLBackend(nullptr, LinuxOpenGLBackend::GLX) == LinuxOpenGLBackend::GLX,
          "a captured GLX context must use GLX, including GLES and XWayland");
    check(LinuxChooseOpenGLBackend(nullptr, LinuxOpenGLBackend::EGL) == LinuxOpenGLBackend::EGL,
          "a captured EGL context must use EGL even if DISPLAY is set");
    check(LinuxChooseOpenGLBackend("egl", LinuxOpenGLBackend::GLX) == LinuxOpenGLBackend::EGL,
          "explicit EGL override must win");
    check(LinuxChooseOpenGLBackend("GLX", LinuxOpenGLBackend::Unknown) == LinuxOpenGLBackend::GLX,
          "backend override must be case insensitive");
    check(LinuxChooseOpenGLBackend("invalid", LinuxOpenGLBackend::EGL) == LinuxOpenGLBackend::EGL,
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

void testExactDrmMatcher()
{
    const std::vector<LinuxDrmRenderNode> candidates = {
        { "/dev/dri/renderD128", 226, 128 },
        { "/dev/dri/renderD129", 226, 129 },
    };
    LinuxDrmMatchResult result = LinuxMatchDrmRenderNode(candidates, 226, 129);
    check(result && result.path == "/dev/dri/renderD129",
          "Vulkan matcher must select only the exact major/minor pair");

    result = LinuxMatchDrmRenderNode(candidates, 226, 130);
    check(result.error == LinuxDrmMatchError::NoExactMatch && result.path.empty(),
          "Vulkan matcher must never guess when no exact node exists");

    const std::vector<LinuxDrmRenderNode> duplicates = {
        { "/dev/dri/renderD128", 226, 128 },
        { "/dev/dri/by-path/duplicate", 226, 128 },
    };
    result = LinuxMatchDrmRenderNode(duplicates, 226, 128);
    check(result.error == LinuxDrmMatchError::DuplicateExactMatch,
          "duplicate device identities must fail instead of choosing first");

    result = LinuxResolveVulkanDrmRenderNode(
        "/dev/dri", nullptr, false, 226, 128);
    check(result.error == LinuxDrmMatchError::IdentityUnavailable,
          "missing Vulkan render identity must fail explicitly");
}

void testDrmMetadataAndOverrideValidation()
{
    struct stat nullMetadata = {};
    check(stat("/dev/null", &nullMetadata) == 0,
          "stat /dev/null fixture");
    const uint32_t expectedMajor = static_cast<uint32_t>(major(nullMetadata.st_rdev));
    const uint32_t expectedMinor = static_cast<uint32_t>(minor(nullMetadata.st_rdev));

    LinuxDrmMatchResult result = LinuxResolveVulkanDrmRenderNode(
        "/dev/dri", "/dev/null", true, expectedMajor, expectedMinor);
    check(result && result.path == "/dev/null",
          "matching exclusive override must pass exact device validation");

    result = LinuxResolveVulkanDrmRenderNode(
        "/dev/dri", "/dev/null", true, expectedMajor, expectedMinor + 1);
    check(result.error == LinuxDrmMatchError::OverrideMismatch,
          "mismatched exclusive override must fail");

    result = LinuxResolveVulkanDrmRenderNode(
        "/dev/dri", "/definitely/missing/render-node", true,
        expectedMajor, expectedMinor);
    check(result.error == LinuxDrmMatchError::InvalidNode,
          "inaccessible override must fail without fallback");

    char directoryTemplate[] = "/tmp/vlc-unity-drm-metadata-XXXXXX";
    char* directory = mkdtemp(directoryTemplate);
    check(directory != nullptr, "create DRM metadata fixture directory");
    if (!directory)
        return;
    const std::string root(directory);
    const std::string regular = root + "/regular";
    const std::string symlinkPath = root + "/by-path-render";
    touch(regular);

    LinuxDrmRenderNode node;
    std::string diagnostic;
    check(!LinuxInspectDrmRenderNode(regular, node, diagnostic),
          "regular files must be rejected as DRM render nodes");
    check(!LinuxInspectDrmRenderNode(root + "/missing", node, diagnostic),
          "failed stat must be reported");

    check(symlink("/dev/null", symlinkPath.c_str()) == 0,
          "create by-path symlink fixture");
    result = LinuxResolveVulkanDrmRenderNode(
        "/dev/dri", symlinkPath.c_str(), true, expectedMajor, expectedMinor);
    check(result && result.path == symlinkPath,
          "by-path symlink to the exact character device must pass");

    unlink(symlinkPath.c_str());
    unlink(regular.c_str());
    rmdir(root.c_str());
}

} // namespace

int main()
{
    testBackendSelection();
    testRenderNodeEnumeration();
    testOverrideAndCompatibilityProbe();
    testExactDrmMatcher();
    testDrmMetadataAndOverrideValidation();

    if (failures != 0)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
