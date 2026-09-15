#include "LinuxGraphicsInterop.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>

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
                                            LinuxOpenGLBackend unityBinding)
{
    if (equalsIgnoreCase(overrideValue, "egl"))
        return LinuxOpenGLBackend::EGL;
    if (equalsIgnoreCase(overrideValue, "glx"))
        return LinuxOpenGLBackend::GLX;

    return unityBinding;
}

const char* LinuxOpenGLBackendName(LinuxOpenGLBackend backend)
{
    return backend == LinuxOpenGLBackend::EGL ? "EGL" :
           backend == LinuxOpenGLBackend::GLX ? "GLX" : "unknown";
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

bool LinuxInspectDrmRenderNode(const std::string& path,
                               LinuxDrmRenderNode& node,
                               std::string& diagnostic)
{
    struct stat metadata = {};
    if (stat(path.c_str(), &metadata) != 0) {
        diagnostic = "stat failed for " + path;
        return false;
    }
    if (!S_ISCHR(metadata.st_mode)) {
        diagnostic = path + " is not a character device";
        return false;
    }
    node.path = path;
    node.deviceMajor = static_cast<uint32_t>(major(metadata.st_rdev));
    node.deviceMinor = static_cast<uint32_t>(minor(metadata.st_rdev));
    diagnostic.clear();
    return true;
}

LinuxDrmMatchResult LinuxMatchDrmRenderNode(
    const std::vector<LinuxDrmRenderNode>& candidates,
    uint32_t expectedMajor,
    uint32_t expectedMinor)
{
    LinuxDrmMatchResult result;
    result.inspectedNodes = candidates;
    if (candidates.empty()) {
        result.error = LinuxDrmMatchError::NoCandidates;
        result.diagnostic = "no valid DRM render-node candidates";
        return result;
    }

    const LinuxDrmRenderNode* match = nullptr;
    for (const LinuxDrmRenderNode& candidate : candidates) {
        if (candidate.deviceMajor != expectedMajor ||
            candidate.deviceMinor != expectedMinor) {
            continue;
        }
        if (match) {
            result.error = LinuxDrmMatchError::DuplicateExactMatch;
            std::ostringstream message;
            message << "multiple DRM nodes resolve to " << expectedMajor << ':'
                    << expectedMinor << ": " << match->path << ", "
                    << candidate.path;
            result.diagnostic = message.str();
            return result;
        }
        match = &candidate;
    }

    if (!match) {
        result.error = LinuxDrmMatchError::NoExactMatch;
        std::ostringstream message;
        message << "no DRM render node exactly matches " << expectedMajor
                << ':' << expectedMinor << "; inspected";
        for (const LinuxDrmRenderNode& candidate : candidates) {
            message << ' ' << candidate.path << '=' << candidate.deviceMajor
                    << ':' << candidate.deviceMinor;
        }
        result.diagnostic = message.str();
        return result;
    }

    result.error = LinuxDrmMatchError::ExactMatch;
    result.path = match->path;
    return result;
}

LinuxDrmMatchResult LinuxResolveVulkanDrmRenderNode(
    const std::string& driDirectory,
    const char* overridePath,
    bool hasRenderIdentity,
    uint32_t expectedMajor,
    uint32_t expectedMinor)
{
    LinuxDrmMatchResult result;
    if (!hasRenderIdentity) {
        result.error = LinuxDrmMatchError::IdentityUnavailable;
        result.diagnostic = "VK_EXT_physical_device_drm did not report a render-node identity";
        return result;
    }

    if (hasValue(overridePath)) {
        LinuxDrmRenderNode node;
        if (!LinuxInspectDrmRenderNode(overridePath, node, result.diagnostic)) {
            result.error = LinuxDrmMatchError::InvalidNode;
            return result;
        }
        result.inspectedNodes.push_back(node);
        if (node.deviceMajor != expectedMajor || node.deviceMinor != expectedMinor) {
            result.error = LinuxDrmMatchError::OverrideMismatch;
            std::ostringstream message;
            message << "VLC_UNITY_DRM_DEVICE=" << overridePath << " resolves to "
                    << node.deviceMajor << ':' << node.deviceMinor
                    << " but Unity Vulkan reports " << expectedMajor << ':'
                    << expectedMinor;
            result.diagnostic = message.str();
            return result;
        }
        result.error = LinuxDrmMatchError::ExactMatch;
        result.path = overridePath;
        return result;
    }

    const std::vector<std::string> paths = LinuxEnumerateDrmRenderNodes(driDirectory);
    std::string firstInvalid;
    for (const std::string& path : paths) {
        LinuxDrmRenderNode node;
        std::string diagnostic;
        if (LinuxInspectDrmRenderNode(path, node, diagnostic))
            result.inspectedNodes.push_back(node);
        else if (firstInvalid.empty())
            firstInvalid = diagnostic;
    }
    if (result.inspectedNodes.empty() && !paths.empty()) {
        result.error = LinuxDrmMatchError::InvalidNode;
        result.diagnostic = firstInvalid;
        return result;
    }
    return LinuxMatchDrmRenderNode(
        result.inspectedNodes, expectedMajor, expectedMinor);
}

const char* LinuxDrmMatchErrorName(LinuxDrmMatchError error)
{
    switch (error) {
    case LinuxDrmMatchError::ExactMatch: return "none";
    case LinuxDrmMatchError::IdentityUnavailable: return "identity-unavailable";
    case LinuxDrmMatchError::NoCandidates: return "no-candidates";
    case LinuxDrmMatchError::InvalidNode: return "invalid-node";
    case LinuxDrmMatchError::NoExactMatch: return "no-exact-match";
    case LinuxDrmMatchError::DuplicateExactMatch: return "duplicate-exact-match";
    case LinuxDrmMatchError::OverrideMismatch: return "override-mismatch";
    }
    return "unknown";
}
