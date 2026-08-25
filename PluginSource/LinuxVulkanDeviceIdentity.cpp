#include "LinuxVulkanDeviceIdentity.h"

#include <iomanip>
#include <sstream>

namespace {

bool isZeroUuid(const std::array<uint8_t, 16>& uuid)
{
    for (uint8_t value : uuid) {
        if (value != 0)
            return false;
    }
    return true;
}

} // namespace

bool LinuxValidateVulkanDrmIdentity(const LinuxVulkanDrmIdentity& identity,
                                    std::string& diagnostic)
{
    if (!identity.extensionAvailable) {
        diagnostic = "VK_EXT_physical_device_drm is unavailable";
        return false;
    }
    if (!identity.hasRender) {
        diagnostic = "VK_EXT_physical_device_drm did not report a render node";
        return false;
    }
    diagnostic.clear();
    return true;
}

bool LinuxValidateGraphicsDeviceUuids(
    bool glUuidExtensionAvailable,
    bool glQuerySucceeded,
    const LinuxGraphicsDeviceUuids& vulkan,
    const LinuxGraphicsDeviceUuids& gl,
    std::string& diagnostic)
{
    if (!glUuidExtensionAvailable) {
        diagnostic = "GL_EXT_memory_object device UUID queries are unavailable";
        return false;
    }
    if (!glQuerySucceeded) {
        diagnostic = "GL device/driver UUID query failed";
        return false;
    }
    if (isZeroUuid(vulkan.device) || isZeroUuid(vulkan.driver) ||
        isZeroUuid(gl.device) || isZeroUuid(gl.driver)) {
        diagnostic = "GL/Vulkan device or driver UUID is invalid";
        return false;
    }
    if (vulkan.device != gl.device) {
        diagnostic = "GL/Vulkan deviceUUID mismatch";
        return false;
    }
    if (vulkan.driver != gl.driver) {
        diagnostic = "GL/Vulkan driverUUID mismatch";
        return false;
    }
    diagnostic.clear();
    return true;
}

bool LinuxValidateOpaqueFdSemaphoreSupport(
    bool querySucceeded,
    bool exportable,
    bool importable,
    bool opaqueFdCompatible,
    std::string& diagnostic)
{
    if (!querySucceeded) {
        diagnostic = "opaque-fd external semaphore capability query failed";
        return false;
    }
    if (!exportable) {
        diagnostic = "opaque-fd external semaphores are not exportable";
        return false;
    }
    if (!importable) {
        diagnostic = "opaque-fd external semaphores are not importable";
        return false;
    }
    if (!opaqueFdCompatible) {
        diagnostic = "opaque-fd is absent from compatible semaphore handle types";
        return false;
    }
    diagnostic.clear();
    return true;
}

std::string LinuxFormatUuid(const std::array<uint8_t, 16>& uuid)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (size_t i = 0; i < uuid.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            output << '-';
        output << std::setw(2) << static_cast<unsigned>(uuid[i]);
    }
    return output.str();
}
