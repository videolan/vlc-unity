#pragma once

class UniqueFd
{
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : m_fd(fd) {}
    ~UniqueFd();

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept;
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    int get() const { return m_fd; }
    explicit operator bool() const { return m_fd >= 0; }
    int release();
    void reset(int fd = -1);

private:
    int m_fd = -1;
};
