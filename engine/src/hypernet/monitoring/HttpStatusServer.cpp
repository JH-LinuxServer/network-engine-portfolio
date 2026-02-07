#include <hypernet/monitoring/HttpStatusServer.hpp>

#include <hypernet/core/Logger.hpp>
#include <hypernet/monitoring/Metrics.hpp>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <string>
#include <string_view>
#include <unistd.h> // pipe, close, read, write

namespace hypernet::monitoring
{

namespace
{
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

inline bool startsWith(std::string_view s, std::string_view prefix) noexcept
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}
} // namespace

HttpStatusServer::HttpStatusServer(std::string bindIp, std::uint16_t port) noexcept
    : bindIp_(std::move(bindIp)), port_(port)
{
}

HttpStatusServer::~HttpStatusServer() noexcept
{
    stopAndJoin();
}

bool HttpStatusServer::start() noexcept
{
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true))
    {
        return true; // 이미 시작됨
    }

    stopRequested_.store(false, std::memory_order_release);

    if (::pipe(wakeupPipe_.data()) != 0)
    {
        SLOG_ERROR("MetricsHTTP", "PipeFailed", "errno={} msg='{}'", errno, std::strerror(errno));
        started_.store(false, std::memory_order_release);
        return false;
    }

    th_ = std::thread([this] { threadMain_(); });
    return true;
}

void HttpStatusServer::requestStop() noexcept
{
    if (!started_.load(std::memory_order_acquire))
    {
        return;
    }

    // 최초 1회만 wakeup
    if (stopRequested_.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }

    if (wakeupPipe_[1] >= 0)
    {
        const unsigned char b = 1;
        // best-effort
        (void)::write(wakeupPipe_[1], &b, 1);
    }
}

void HttpStatusServer::stopAndJoin() noexcept
{
    requestStop();

    if (th_.joinable())
    {
        th_.join();
    }

    listenSock_.close();
    closeWakeupPipe_();

    started_.store(false, std::memory_order_release);
}

void HttpStatusServer::closeWakeupPipe_() noexcept
{
    for (int &fd : wakeupPipe_)
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }
}

void HttpStatusServer::threadMain_() noexcept
{
    listenSock_ = hypernet::net::Socket::createTcpIPv4();
    if (!listenSock_.isValid())
    {
        SLOG_ERROR("MetricsHTTP", "SocketCreateFailed", "errno={} msg='{}'", errno,
                   std::strerror(errno));
        return;
    }

    (void)listenSock_.setReuseAddr(true);
    (void)listenSock_.setNonBlocking(true);

    if (!listenSock_.bind(bindIp_, port_))
    {
        SLOG_ERROR("MetricsHTTP", "BindFailed", "addr={}:{} errno={} msg='{}'", bindIp_, port_,
                   errno, std::strerror(errno));
        return;
    }

    if (!listenSock_.listen(64))
    {
        SLOG_ERROR("MetricsHTTP", "ListenFailed", "errno={} msg='{}'", errno, std::strerror(errno));
        return;
    }

    SLOG_INFO("MetricsHTTP", "Listening", "url=http://{}:{}/metrics", bindIp_, port_);

    pollfd fds[2]{};
    fds[0].fd = listenSock_.nativeHandle();
    fds[0].events = POLLIN;

    fds[1].fd = wakeupPipe_[0];
    fds[1].events = POLLIN;

    while (!stopRequested_.load(std::memory_order_acquire))
    {
        const int rc = ::poll(fds, 2, -1);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;

            SLOG_ERROR("MetricsHTTP", "PollFailed", "errno={} msg='{}'", errno,
                       std::strerror(errno));
            break;
        }

        if (fds[1].revents & POLLIN)
        {
            // drain pipe
            unsigned char tmp[32];
            (void)::read(wakeupPipe_[0], tmp, sizeof(tmp));
            break;
        }

        if (fds[0].revents & POLLIN)
        {
            acceptLoop_();
        }
    }

    listenSock_.close();
    SLOG_INFO("MetricsHTTP", "Stopped", "");
}

void HttpStatusServer::acceptLoop_() noexcept
{
    for (;;)
    {
        hypernet::net::Socket conn = listenSock_.accept();
        if (!conn.isValid())
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return; // 더 없음
            }
            if (errno == EINTR)
            {
                continue;
            }

            SLOG_WARN("MetricsHTTP", "AcceptFailed", "errno={} msg='{}'", errno,
                      std::strerror(errno));
            return;
        }

        handleClient_(std::move(conn));
    }
}

bool HttpStatusServer::parseRequestTarget_(const char *buf, std::size_t len,
                                           std::string_view &outMethod,
                                           std::string_view &outTarget) noexcept
{
    std::string_view req{buf, len};

    // 첫 줄만 파싱: "GET /path HTTP/1.1"
    std::size_t eol = req.find("\r\n");
    if (eol == std::string_view::npos)
    {
        eol = req.find('\n');
        if (eol == std::string_view::npos)
            return false;
    }

    const std::string_view line = req.substr(0, eol);

    const std::size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos)
        return false;

    const std::size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos)
        return false;

    outMethod = line.substr(0, sp1);
    outTarget = line.substr(sp1 + 1, sp2 - (sp1 + 1));
    return true;
}

void HttpStatusServer::sendAll_(hypernet::net::Socket &sock, const char *data,
                                std::size_t len) noexcept
{
    std::size_t off = 0;
    while (off < len)
    {
        const ::ssize_t n = sock.send(data + off, len - off, kSendFlags);
        if (n > 0)
        {
            off += static_cast<std::size_t>(n);
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            pollfd p{};
            p.fd = sock.nativeHandle();
            p.events = POLLOUT;
            (void)::poll(&p, 1, 100); // best-effort
            continue;
        }

        return; // 실패/끊김
    }
}

void HttpStatusServer::sendTextResponse_(hypernet::net::Socket &sock, int code,
                                         std::string_view reason, std::string_view contentType,
                                         std::string &&body) noexcept
{
    std::string header;
    header.reserve(256);

    header += "HTTP/1.1 ";
    header += std::to_string(code);
    header += " ";
    header += reason;
    header += "\r\n";

    header += "Content-Type: ";
    header += contentType;
    header += "\r\n";

    header += "Content-Length: ";
    header += std::to_string(body.size());
    header += "\r\n";

    header += "Connection: close\r\n\r\n";

    sendAll_(sock, header.data(), header.size());
    sendAll_(sock, body.data(), body.size());
}

void HttpStatusServer::handleClient_(hypernet::net::Socket &&conn) noexcept
{
    // conn은 accept4로 NONBLOCK일 수 있으니, 짧게 poll 후 읽는다.
    pollfd p{};
    p.fd = conn.nativeHandle();
    p.events = POLLIN;

    const int rc = ::poll(&p, 1, 500);
    if (rc <= 0 || !(p.revents & POLLIN))
    {
        return;
    }

    char buf[4096];
    const ::ssize_t n = conn.recv(buf, sizeof(buf), 0);
    if (n <= 0)
    {
        return;
    }

    std::string_view method;
    std::string_view target;
    if (!parseRequestTarget_(buf, static_cast<std::size_t>(n), method, target))
    {
        sendTextResponse_(conn, 400, "Bad Request", "text/plain; charset=utf-8", "bad request\n");
        return;
    }

    if (method != "GET")
    {
        sendTextResponse_(conn, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                          "method not allowed\n");
        return;
    }

    // /metrics 또는 /metrics?...
    if (target == "/metrics" || startsWith(target, "/metrics?"))
    {
        std::string body = hypernet::monitoring::engineMetrics().toPrometheusText();
        sendTextResponse_(conn, 200, "OK", "text/plain; version=0.0.4; charset=utf-8",
                          std::move(body));
        return;
    }

    sendTextResponse_(conn, 404, "Not Found", "text/plain; charset=utf-8", "not found\n");
}

} // namespace hypernet::monitoring