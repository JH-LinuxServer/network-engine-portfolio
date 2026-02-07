#include <hypernet/net/EpollReactor.hpp>

#include <hypernet/core/Logger.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <system_error>
#include <unistd.h>

namespace hypernet::net
{

EpollReactor::EpollReactor(int maxEvents) : maxEvents_(maxEvents)
{
    if (maxEvents_ <= 0)
    {
        maxEvents_ = 64;
    }

    epollFd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(),
                                "EpollReactor: epoll_create1 failed");
    }

    eventBuffer_.resize(static_cast<std::size_t>(maxEvents_));

    SLOG_INFO("EpollReactor", "Created", "fd={} max_events={}", epollFd_, maxEvents_);
}

EpollReactor::~EpollReactor() noexcept
{
    if (epollFd_ >= 0)
    {
        ::close(epollFd_);
        SLOG_INFO("EpollReactor", "Closed", "fd={}", epollFd_);
        epollFd_ = -1;
    }
}

bool EpollReactor::registerFd(Fd fd, std::uint32_t events) noexcept
{
    if (epollFd_ < 0 || fd < 0)
    {
        errno = EBADF;
        SLOG_ERROR("EpollReactor", "RegisterFdFailed", "reason=InvalidFd epoll_fd={} fd={}",
                   epollFd_, fd);
        return false;
    }

    ::epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        SLOG_ERROR("EpollReactor", "CtlAddFailed", "fd={} errno={} msg='{}'", fd, errno,
                   std::strerror(errno));
        return false;
    }

    SLOG_DEBUG("EpollReactor", "Registered", "fd={} events=0x{:x}", fd, events);
    return true;
}

bool EpollReactor::modifyFd(Fd fd, std::uint32_t events) noexcept
{
    if (epollFd_ < 0 || fd < 0)
    {
        errno = EBADF;
        SLOG_ERROR("EpollReactor", "ModifyFdFailed", "reason=InvalidFd epoll_fd={} fd={}", epollFd_,
                   fd);
        return false;
    }

    ::epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        SLOG_ERROR("EpollReactor", "CtlModFailed", "fd={} errno={} msg='{}'", fd, errno,
                   std::strerror(errno));
        return false;
    }

    SLOG_DEBUG("EpollReactor", "Modified", "fd={} events=0x{:x}", fd, events);
    return true;
}

bool EpollReactor::unregisterFd(Fd fd) noexcept
{
    if (epollFd_ < 0 || fd < 0)
    {
        errno = EBADF;
        SLOG_ERROR("EpollReactor", "UnregisterFdFailed", "reason=InvalidFd epoll_fd={} fd={}",
                   epollFd_, fd);
        return false;
    }

    if (::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1)
    {
        if (errno == ENOENT || errno == EBADF)
        {
            SLOG_WARN("EpollReactor", "CtlDelFailed", "fd={} errno={} msg='{}'", fd, errno,
                      std::strerror(errno));
        }
        else
        {
            SLOG_ERROR("EpollReactor", "CtlDelFailed", "fd={} errno={} msg='{}'", fd, errno,
                       std::strerror(errno));
        }
        return false;
    }

    SLOG_DEBUG("EpollReactor", "Unregistered", "fd={}", fd);
    return true;
}

int EpollReactor::wait(ReadyEvent *outEvents, int maxEvents, int timeoutMs) noexcept
{
    if (epollFd_ < 0)
    {
        errno = EBADF;
        SLOG_ERROR("EpollReactor", "WaitFailed", "reason=InvalidEpollFd");
        return -1;
    }

    if (!outEvents || maxEvents <= 0)
    {
        errno = EINVAL;
        SLOG_ERROR("EpollReactor", "WaitFailed", "reason=InvalidArgs");
        return -1;
    }

    const int maxPollEvents = std::min(maxEvents, maxEvents_);

    int n = ::epoll_wait(epollFd_, eventBuffer_.data(), maxPollEvents, timeoutMs);
    if (n < 0)
    {
        if (errno == EINTR)
        {
            SLOG_DEBUG("EpollReactor", "WaitInterrupted", "reason=EINTR");
        }
        else
        {
            SLOG_ERROR("EpollReactor", "WaitFailed", "errno={} msg='{}'", errno,
                       std::strerror(errno));
        }
        return -1;
    }

    for (int i = 0; i < n; ++i)
    {
        const auto &ev = eventBuffer_[i];
        outEvents[i].fd = ev.data.fd;
        outEvents[i].events = ev.events;
    }

    return n;
}

} // namespace hypernet::net