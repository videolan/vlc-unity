#include "UniqueFd.h"

#include <unistd.h>

UniqueFd::~UniqueFd()
{
    reset();
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : m_fd(other.release())
{
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
    if (this != &other) {
        reset();
        m_fd = other.release();
    }
    return *this;
}

int UniqueFd::release()
{
    const int fd = m_fd;
    m_fd = -1;
    return fd;
}

void UniqueFd::reset(int fd)
{
    if (m_fd >= 0)
        close(m_fd);
    m_fd = fd;
}
