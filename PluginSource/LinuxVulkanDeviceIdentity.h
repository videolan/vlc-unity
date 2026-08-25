#pragma once

#include <array>
#include <cstdint>
#include <string>

struct LinuxVulkanDrmIdentity
{
    bool extensionAvailable = false;
    bool hasRender = false;
    uint32_t renderMajor = 0;
    uint32_t renderMinor = 0;
};

struct LinuxGraphicsDeviceUuids
{
    std::array<uint8_t, 16> device = {};
    std::array<uint8_t, 16> driver = {};
};

bool LinuxValidateVulkanDrmIdentity(const LinuxVulkanDrmIdentity& identity,
                                    std::string& diagnostic);

bool LinuxValidateGraphicsDeviceUuids(
    bool glUuidExtensionAvailable,
    bool glQuerySucceeded,
    const LinuxGraphicsDeviceUuids& vulkan,
    const LinuxGraphicsDeviceUuids& gl,
    std::string& diagnostic);

bool LinuxValidateOpaqueFdSemaphoreSupport(
    bool querySucceeded,
    bool exportable,
    bool importable,
    bool opaqueFdCompatible,
    std::string& diagnostic);

std::string LinuxFormatUuid(const std::array<uint8_t, 16>& uuid);
