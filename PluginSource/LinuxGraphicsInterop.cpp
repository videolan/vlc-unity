#include "LinuxGraphicsInterop.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>

namespace {

bool equalsIgnoreCase(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
        return false;

    while (*lhs && *rhs) {
        const unsigned char lhsChar = static_cast<unsigned char>(*lhs);
        const unsigned char rhsChar = static_cast<unsigned char>(*rhs);
        if (std::tolower(lhsChar) != std::tolower(rhsChar))
            return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

bool hasValue(const char* value)
{
    return value && value[0] != '\0';
}

bool isRenderNodeName(const char* name)
{
    static const char prefix[] = "renderD";
    static const size_t prefixLength = sizeof(prefix) - 1;

    if (!name || strncmp(name, prefix, prefixLength) != 0)
        return false;

    const char* suffix = name + prefixLength;
    if (*suffix == '\0')
        return false;

    while (*suffix) {
        if (!std::isdigit(static_cast<unsigned char>(*suffix)))
            return false;
        ++suffix;
    }
    return true;
}

const char* renderNodeMinor(const std::string& path)
{
    const size_t separator = path.find_last_of('/');
    const size_t nameStart = separator == std::string::npos ? 0 : separator + 1;
    return path.c_str() + nameStart + strlen("renderD");
}

bool renderNodeLess(const std::string& lhs, const std::string& rhs)
{
    const char* lhsMinor = renderNodeMinor(lhs);
    const char* rhsMinor = renderNodeMinor(rhs);

    while (*lhsMinor == '0' && lhsMinor[1] != '\0')
        ++lhsMinor;
    while (*rhsMinor == '0' && rhsMinor[1] != '\0')
        ++rhsMinor;

    const size_t lhsLength = strlen(lhsMinor);
    const size_t rhsLength = strlen(rhsMinor);
    if (lhsLength != rhsLength)
        return lhsLength < rhsLength;
    return strcmp(lhsMinor, rhsMinor) < 0;
}

std::string joinPath(const std::string& directory, const char* name)
{
    if (!directory.empty() && directory.back() == '/')
        return directory + name;
    return directory + "/" + name;
}

} // namespace

LinuxOpenGLBackend LinuxChooseOpenGLBackend(const char* overrideValue,
                                            const char* x11Display,
                                            const char* waylandDisplay)
{
    if (equalsIgnoreCase(overrideValue, "egl"))
        return LinuxOpenGLBackend::EGL;
    if (equalsIgnoreCase(overrideValue, "glx"))
        return LinuxOpenGLBackend::GLX;

    // A Wayland desktop still exports WAYLAND_DISPLAY to XWayland clients.
    // DISPLAY tells us that Unity can use GLX, which permits direct context
    // sharing and avoids cross-driver DMA-BUF imports.
    if (hasValue(x11Display))
        return LinuxOpenGLBackend::GLX;
    if (hasValue(waylandDisplay))
        return LinuxOpenGLBackend::EGL;
    return LinuxOpenGLBackend::GLX;
}

const char* LinuxOpenGLBackendName(LinuxOpenGLBackend backend)
{
    return backend == LinuxOpenGLBackend::EGL ? "EGL" : "GLX";
}

bool LinuxIsOpenGLBackendOverrideValid(const char* overrideValue)
{
    return !hasValue(overrideValue) ||
           equalsIgnoreCase(overrideValue, "glx") ||
           equalsIgnoreCase(overrideValue, "egl");
}

bool LinuxEnvironmentFlagEnabled(const char* value)
{
    return equalsIgnoreCase(value, "1") ||
           equalsIgnoreCase(value, "true") ||
           equalsIgnoreCase(value, "yes") ||
           equalsIgnoreCase(value, "on");
}

std::vector<std::string> LinuxEnumerateDrmRenderNodes(const std::string& driDirectory)
{
    std::vector<std::string> result;
    DIR* directory = opendir(driDirectory.c_str());
    if (!directory)
        return result;

    while (dirent* entry = readdir(directory)) {
        if (isRenderNodeName(entry->d_name))
            result.push_back(joinPath(driDirectory, entry->d_name));
    }
    closedir(directory);

    std::sort(result.begin(), result.end(), renderNodeLess);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<std::string> LinuxBuildDrmDeviceCandidates(const std::string& driDirectory,
                                                       const char* overridePath)
{
    if (hasValue(overridePath))
        return { overridePath };
    return LinuxEnumerateDrmRenderNodes(driDirectory);
}

std::string LinuxSelectCompatibleDrmDevice(
    const std::vector<std::string>& candidates,
    const std::function<bool(const std::string&)>& probe)
{
    if (!probe)
        return {};

    for (const std::string& candidate : candidates) {
        if (probe(candidate))
            return candidate;
    }
    return {};
}
