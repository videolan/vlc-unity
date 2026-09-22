#pragma once

#include <string>

struct gbm_device;

class LinuxGBMDevice
{
public:
    LinuxGBMDevice() = default;
    ~LinuxGBMDevice();

    LinuxGBMDevice(const LinuxGBMDevice&) = delete;
    LinuxGBMDevice& operator=(const LinuxGBMDevice&) = delete;

    bool open(const char* logPrefix, const std::string& path);
    void reset();

    gbm_device* get() const { return m_device; }
    int fd() const { return m_fd; }
    const std::string& path() const { return m_path; }
    explicit operator bool() const { return m_device != nullptr; }

private:
    int m_fd = -1;
    gbm_device* m_device = nullptr;
    std::string m_path;
};
