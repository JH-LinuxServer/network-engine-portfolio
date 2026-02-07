#include <hypernet/net/Socket.hpp>

#include <arpa/inet.h>   // inet_pton
#include <cerrno>        // errno
#include <cstring>       // std::memset
#include <fcntl.h>       // fcntl, O_NONBLOCK
#include <netinet/in.h>  // sockaddr_in, AF_INET, AF_INET6
#include <netinet/tcp.h> // TCP_NODELAY
#include <unistd.h>      // close, read, write

namespace hypernet::net
{

Socket::Socket(Handle fd) noexcept : fd_(fd) {}

Socket::~Socket() noexcept
{
    // 소멸자에서는 예외를 던지면 안 되므로, best-effort 로 close 만 수행합니다.
    close();
}

Socket::Socket(Socket &&other) noexcept : fd_(other.fd_)
{
    other.fd_ = -1;
}

Socket &Socket::operator=(Socket &&other) noexcept
{
    if (this != &other)
    {
        // 현재 소켓이 유효하다면 먼저 닫고, 새 fd 를 받아온다.
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

Socket Socket::createTcpIPv4() noexcept
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return Socket{};
    }
    return Socket{fd};
}

Socket Socket::createTcpIPv6() noexcept
{
    const int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return Socket{};
    }
    return Socket{fd};
}

void Socket::close() noexcept
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Socket::setNonBlocking(bool enable) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    const int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags == -1)
    {
        return false;
    }

    int newFlags = flags;
    if (enable)
    {
        newFlags |= O_NONBLOCK;
    }
    else
    {
        newFlags &= ~O_NONBLOCK;
    }

    if (::fcntl(fd_, F_SETFL, newFlags) == -1)
    {
        return false;
    }

    return true;
}

bool Socket::setReuseAddr(bool enable) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    const int opt = enable ? 1 : 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        return false;
    }

    return true;
}

bool Socket::setReusePort(bool enable) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

#ifdef SO_REUSEPORT
    const int opt = enable ? 1 : 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1)
    {
        return false;
    }
    return true;
#else
    (void)enable;
    errno = ENOTSUP;
    return false;
#endif
}

bool Socket::setNoDelay(bool enable) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    const int opt = enable ? 1 : 0;
    if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
    {
        return false;
    }
    return true;
}

bool Socket::bind(const ::sockaddr *addr, ::socklen_t len) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    if (::bind(fd_, addr, len) == -1)
    {
        return false;
    }
    return true;
}

bool Socket::bind(const std::string &ip, std::uint16_t port) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    ::sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // inet_pton 은 성공 시 1, 실패 시 0 또는 -1 을 반환합니다.
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
    {
        errno = EINVAL;
        return false;
    }

    return bind(reinterpret_cast<::sockaddr *>(&addr), sizeof(addr));
}

bool Socket::listen(int backlog) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    if (::listen(fd_, backlog) == -1)
    {
        return false;
    }
    return true;
}

Socket Socket::accept(::sockaddr *addr, ::socklen_t *len) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return Socket{};
    }

#if defined(__linux__)
    // Architecture Contract에 맞춰 accept4 + NONBLOCK + CLOEXEC를 우선 시도한다.
    int newFd = ::accept4(fd_, addr, len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (newFd < 0)
    {
        // 매우 보수적으로 ENOSYS(커널 미지원)면 accept로 폴백한다.
        if (errno == ENOSYS)
        {
            newFd = ::accept(fd_, addr, len);
        }
    }
#else
    int newFd = ::accept(fd_, addr, len);
#endif

    if (newFd < 0)
    {
        return Socket{};
    }

    return Socket{newFd};
}

Socket Socket::accept() noexcept
{
    return accept(nullptr, nullptr);
}

bool Socket::connect(const ::sockaddr *addr, ::socklen_t len) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    if (::connect(fd_, addr, len) == -1)
    {
        return false;
    }

    return true;
}

bool Socket::connect(const std::string &ip, std::uint16_t port) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return false;
    }

    ::sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
    {
        errno = EINVAL;
        return false;
    }

    return connect(reinterpret_cast<::sockaddr *>(&addr), sizeof(addr));
}

::ssize_t Socket::send(const void *data, std::size_t len, int flags) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return -1;
    }

    if (!data || len == 0)
    {
        // send(2) 의 의미에 맞추어, len == 0 이면 0 을 반환해도 무방하지만,
        // 여기서는 시스템 콜을 한번 타도록 그대로 위임한다.
        return ::send(fd_, data, len, flags);
    }

    return ::send(fd_, data, len, flags);
}

::ssize_t Socket::recv(void *buffer, std::size_t len, int flags) noexcept
{
    if (!isValid())
    {
        errno = EBADF;
        return -1;
    }

    // 지울 예정
    // if (!buffer || len == 0) {
    //     return ::recv(fd_, buffer, len, flags);
    // }

    return ::recv(fd_, buffer, len, flags);
}

} // namespace hypernet::net
