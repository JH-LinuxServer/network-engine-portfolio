#include <hypernet/net/Acceptor.hpp>

#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/net/EventLoop.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <system_error>
#include <utility>

namespace hypernet::net
{

namespace
{

[[noreturn]] void throwSysError(const char *what)
{
    throw std::system_error(errno, std::generic_category(), what);
}

const char *boolOnOff(bool v) noexcept
{
    return v ? "on" : "off";
}
} // namespace

Acceptor::Acceptor(std::string listenAddress, std::uint16_t listenPort, int backlog, bool reusePort)
    : listenAddress_(std::move(listenAddress)), listenPort_(listenPort), backlog_(backlog)
{
    listenSocket_ = Socket::createTcpIPv4();
    if (!listenSocket_.isValid())
    {
        throwSysError("Acceptor: socket(AF_INET, SOCK_STREAM) failed");
    }

    if (!listenSocket_.setReuseAddr(true))
    {
        throwSysError("Acceptor: setsockopt(SO_REUSEADDR) failed");
    }

    bool reusePortApplied = false;
    if (reusePort)
    {
        if (!listenSocket_.setReusePort(true))
        {
            const int e = errno;
            SLOG_ERROR("Acceptor", "ReusePortFailed", "errno={} msg='{}'", e, std::strerror(e));
            throw std::system_error(e, std::generic_category(),
                                    "Acceptor: setsockopt(SO_REUSEPORT) failed");
        }
        reusePortApplied = true;
    }

    if (!listenSocket_.bind(listenAddress_, listenPort_))
    {
        throwSysError("Acceptor: bind() failed");
    }

    refreshBoundPort();

    if (!listenSocket_.listen(backlog_))
    {
        throwSysError("Acceptor: listen() failed");
    }

    SLOG_INFO("Acceptor", "Listening", "addr={} port={} backlog={} reuse_req={} reuse_applied={}",
              listenAddress_, listenPort_, backlog_, boolOnOff(reusePort),
              reusePortApplied ? "yes" : "no");
}

Socket Acceptor::acceptOne(PeerEndpoint *outPeer) noexcept
{
    if (!listenSocket_.isValid())
    {
        errno = EBADF;
        return Socket{};
    }

    ::sockaddr_storage ss{};
    ::socklen_t slen = sizeof(ss);

    Socket client = listenSocket_.accept(reinterpret_cast<::sockaddr *>(&ss), &slen);
    if (!client.isValid())
    {
        return Socket{};
    }

    if (outPeer)
    {
        fillPeerEndpoint(reinterpret_cast<::sockaddr *>(&ss), slen, *outPeer);
    }

    return client;
}

void Acceptor::handleEvent(EventLoop &loop, const EpollReactor::ReadyEvent &ev)
{
    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
    {
        onError_(loop, ev);
        return;
    }

    if (ev.events & EPOLLIN)
    {
        onReadable_();
    }
}

void Acceptor::onReadable_()
{
    for (;;)
    {
        PeerEndpoint peer{};
        auto client = acceptOne(&peer);

        if (!client.isValid())
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }

            SLOG_ERROR("Acceptor", "AcceptFailed", "errno={} msg='{}'", errno,
                       std::strerror(errno));
            break;
        }

        (void)client.setNonBlocking(true);

        SLOG_INFO("Acceptor", "Accepted", "peer_ip={} peer_port={} fd={}", peer.ip, peer.port,
                  client.nativeHandle());

        if (onAccept_)
        {
            try
            {
                onAccept_(std::move(client), peer);
            }
            catch (const std::exception &e)
            {
                SLOG_ERROR("Acceptor", "OnAcceptException", "what='{}'", e.what());
            }
            catch (...)
            {
                SLOG_ERROR("Acceptor", "OnAcceptException", "type=unknown");
            }
        }
    }
}

void Acceptor::onError_(EventLoop &loop, const EpollReactor::ReadyEvent &ev) noexcept
{
    SLOG_ERROR("Acceptor", "ListenSocketError", "fd={} events=0x{:x} action=removing_listener",
               ev.fd, ev.events);

    (void)loop.removeFd(ev.fd);
    close();
}

void Acceptor::refreshBoundPort() noexcept
{
    if (!listenSocket_.isValid())
    {
        return;
    }

    ::sockaddr_in addr{};
    ::socklen_t len = sizeof(addr);
    if (::getsockname(listenSocket_.nativeHandle(), reinterpret_cast<::sockaddr *>(&addr), &len) ==
        -1)
    {
        return;
    }

    if (addr.sin_family == AF_INET)
    {
        listenPort_ = ntohs(addr.sin_port);
    }
}

void Acceptor::fillPeerEndpoint(const ::sockaddr *sa, ::socklen_t salen, PeerEndpoint &out) noexcept
{
    (void)salen;

    out.ip = "unknown";
    out.port = 0;

    if (!sa)
    {
        return;
    }

    char buf[INET6_ADDRSTRLEN] = {};

    if (sa->sa_family == AF_INET)
    {
        const auto *in = reinterpret_cast<const ::sockaddr_in *>(sa);
        if (::inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf)) != nullptr)
        {
            out.ip = buf;
        }
        out.port = ntohs(in->sin_port);
        return;
    }

    if (sa->sa_family == AF_INET6)
    {
        const auto *in6 = reinterpret_cast<const ::sockaddr_in6 *>(sa);
        if (::inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf)) != nullptr)
        {
            out.ip = buf;
        }
        out.port = ntohs(in6->sin6_port);
        return;
    }
}

} // namespace hypernet::net