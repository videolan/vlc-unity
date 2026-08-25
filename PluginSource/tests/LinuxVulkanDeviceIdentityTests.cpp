#include "../LinuxVulkanDeviceIdentity.h"

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

LinuxGraphicsDeviceUuids makeUuids(uint8_t deviceSeed, uint8_t driverSeed)
{
    LinuxGraphicsDeviceUuids value;
    for (size_t i = 0; i < value.device.size(); ++i) {
        value.device[i] = static_cast<uint8_t>(deviceSeed + i);
        value.driver[i] = static_cast<uint8_t>(driverSeed + i);
    }
    return value;
}

void testDrmCapabilityGate()
{
    std::string diagnostic;
    LinuxVulkanDrmIdentity identity;
    check(!LinuxValidateVulkanDrmIdentity(identity, diagnostic),
          "missing DRM extension must fail");
    identity.extensionAvailable = true;
    check(!LinuxValidateVulkanDrmIdentity(identity, diagnostic),
          "missing hasRender must fail");
    identity.hasRender = true;
    identity.renderMajor = 226;
    identity.renderMinor = 128;
    check(LinuxValidateVulkanDrmIdentity(identity, diagnostic),
          "reported render identity must pass capability gate");
}

void testUuidCompatibilityGate()
{
    const LinuxGraphicsDeviceUuids expected = makeUuids(1, 32);
    LinuxGraphicsDeviceUuids actual = expected;
    std::string diagnostic;
    check(LinuxValidateGraphicsDeviceUuids(
              true, true, expected, actual, diagnostic),
          "exact deviceUUID and driverUUID match must pass");
    actual.device[0] ^= 1;
    check(!LinuxValidateGraphicsDeviceUuids(
              true, true, expected, actual, diagnostic),
          "deviceUUID mismatch must fail");
    actual = expected;
    actual.driver[0] ^= 1;
    check(!LinuxValidateGraphicsDeviceUuids(
              true, true, expected, actual, diagnostic),
          "driverUUID mismatch must fail");
    check(!LinuxValidateGraphicsDeviceUuids(
              false, true, expected, expected, diagnostic),
          "unavailable GL UUID extension must fail");
    check(!LinuxValidateGraphicsDeviceUuids(
              true, false, expected, expected, diagnostic),
          "failed GL UUID query must fail");
    LinuxGraphicsDeviceUuids zero;
    check(!LinuxValidateGraphicsDeviceUuids(
              true, true, zero, zero, diagnostic),
          "all-zero UUIDs must not pass as an exact identity");
}

void testExternalSemaphoreCapabilityGate()
{
    std::string diagnostic;
    check(LinuxValidateOpaqueFdSemaphoreSupport(
              true, true, true, true, diagnostic),
          "bidirectional opaque-fd semaphore support must pass");
    check(!LinuxValidateOpaqueFdSemaphoreSupport(
              false, true, true, true, diagnostic),
          "failed external semaphore query must fail");
    check(!LinuxValidateOpaqueFdSemaphoreSupport(
              true, false, true, true, diagnostic),
          "non-exportable opaque-fd semaphore must fail");
    check(!LinuxValidateOpaqueFdSemaphoreSupport(
              true, true, false, true, diagnostic),
          "non-importable opaque-fd semaphore must fail");
    check(!LinuxValidateOpaqueFdSemaphoreSupport(
              true, true, true, false, diagnostic),
          "incompatible opaque-fd handle type must fail");
}

} // namespace

int main()
{
    testDrmCapabilityGate();
    testUuidCompatibilityGate();
    testExternalSemaphoreCapabilityGate();
    if (failures)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
