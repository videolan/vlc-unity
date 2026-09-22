#include "LinuxGBMDevice.h"
#include "Log.h"

#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>

LinuxGBMDevice::~LinuxGBMDevice()
{
    reset();
}

bool LinuxGBMDevice::open(const char* logPrefix, const std::string& path)
{
    reset();
    m_fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        DEBUG("[%s] could not open DRM render node %s", logPrefix, path.c_str());
        return false;
    }
    m_device = gbm_create_device(m_fd);
    if (!m_device) {
        DEBUG("[%s] gbm_create_device failed for %s", logPrefix, path.c_str());
        reset();
        return false;
    }
    m_path = path;
    const char* backend = gbm_device_get_backend_name(m_device);
    DEBUG("[%s] DRM render node %s uses GBM backend %s",
          logPrefix, path.c_str(), backend ? backend : "?");
    return true;
}

void LinuxGBMDevice::reset()
{
    if (m_device)
        gbm_device_destroy(m_device);
    if (m_fd >= 0)
        close(m_fd);
    m_device = nullptr;
    m_fd = -1;
    m_path.clear();
}
