#include <hypernet/connector/ConnectorManager.hpp>

#include <hypernet/core/Logger.hpp>
#include <hypernet/net/Acceptor.hpp>
#include <hypernet/net/EventLoop.hpp>
#include <hypernet/net/EpollReactor.hpp>
#include <hypernet/net/FdHandler.hpp>
#include <hypernet/net/Socket.hpp>
#include <hypernet/net/SessionManager.hpp>

#include <cerrno>
#include <cstring>
#include <utility>

#include <sys/socket.h>

namespace hypernet::connector
{

// ===========================================================
// Dial State (Outbound TCP, numeric IP only)
// ===========================================================
struct ConnectorManager::DialState final : public hypernet::net::IFdHandler
{
    DialId dialId{0};

    ConnectorManager *owner{nullptr};
    hypernet::net::EventLoop *loop{nullptr};

    DialTcpOptions opt{};
    DialTcpCallback cb{};

    std::uint32_t attemptIndex{0};
    bool connectedEventRegistered{false};
    bool completed{false};

    hypernet::net::Socket sock{};
    hypernet::net::Acceptor::PeerEndpoint peer{};

    [[nodiscard]] const char *fdTag() const noexcept override { return "dial"; }
    [[nodiscard]] std::uint64_t fdDebugId() const noexcept override { return dialId; }

    void handleEvent(hypernet::net::EventLoop & /*evLoop*/, const hypernet::net::EpollReactor::ReadyEvent &ev) override
    {
        if (!owner || !loop)
            return;

        auto it = owner->dials_.find(dialId);
        std::shared_ptr<DialState> keepAlive = (it != owner->dials_.end()) ? it->second : nullptr;
        if (!keepAlive)
            return;

        if (completed)
            return;

        int soErr = 0;
        ::socklen_t slen = sizeof(soErr);
        if (::getsockopt(ev.fd, SOL_SOCKET, SO_ERROR, &soErr, &slen) == -1)
        {
            owner->finishDialFail_(dialId, attemptIndex, "getsockopt(SO_ERROR) failed", false);
            return;
        }

        if (soErr != 0)
        {
            errno = soErr;
            const std::string err = std::strerror(soErr);

            if (opt.retryOnce && attemptIndex == 0)
            {
                const std::uint32_t nextAttempt = attemptIndex + 1;
                const auto delay = std::chrono::milliseconds(opt.retryDelayMs);
                loop->addTimer(delay, [mgr = owner, dialId = dialId, nextAttempt]() noexcept { mgr->dialStartAttempt_(dialId, nextAttempt); });
                return;
            }

            owner->finishDialFail_(dialId, attemptIndex, err, false);
            return;
        }

        owner->finishDialOk_(dialId, attemptIndex);
    }
};

// ===========================================================
// ConnectorManager (existing request/response connector logic)
// ===========================================================
ConnectorManager::ConnectorManager(hypernet::net::EventLoop &loop, hypernet::net::SessionManager &sm) noexcept : loop_(loop), sm_(sm) {}

bool ConnectorManager::add(std::unique_ptr<IConnector> c)
{
    if (!c)
        return false;

    const auto name = std::string(c->name());
    if (name.empty())
        return false;

    if (connectors_.count(name) != 0)
        return false;

    c->setCompletionHook([this](RequestId id, int attempt, Response r) { this->onComplete_(id, attempt, std::move(r)); });

    connectors_.emplace(name, std::move(c));
    return true;
}

IConnector *ConnectorManager::find_(std::string_view name) noexcept
{
    auto it = connectors_.find(std::string(name));
    if (it == connectors_.end())
        return nullptr;
    return it->second.get();
}

std::chrono::milliseconds ConnectorManager::resolveTimeout_(const SendOptions &opt) const noexcept
{
    if (opt.timeoutMs <= 0)
        return defaultTimeout_;
    return std::chrono::milliseconds(opt.timeoutMs);
}

void ConnectorManager::sendAsync(std::string_view name, const SendOptions &opt, std::vector<std::uint8_t> request, Callback cb)
{
    if (!loop_.isInOwnerThread())
    {
        loop_.post([this, name = std::string(name), opt, req = std::move(request), cb = std::move(cb)]() mutable { this->sendAsync(name, opt, std::move(req), std::move(cb)); });
        return;
    }

    auto *c = find_(name);
    if (!c)
    {
        if (cb)
            cb(Response::fail("unknown_connector"));
        return;
    }

    const RequestId id = nextRequestId_++;
    Pending p{};
    p.connectorName = std::string(name);
    p.opt = opt;
    p.request = request;
    p.cb = std::move(cb);
    p.attempt = 0;
    p.timeoutResolved = resolveTimeout_(opt);

    pending_.emplace(id, std::move(p));
    startAttempt_(id);
}

void ConnectorManager::sendAsyncToDispatcher(std::string_view name, const SendOptions &opt, hypernet::SessionHandle session, hypernet::protocol::Dispatcher::OpCode resumeOpcode,
                                             std::vector<std::uint8_t> request)
{
    if (!loop_.isInOwnerThread())
    {
        loop_.post([this, name = std::string(name), opt, session, resumeOpcode, req = std::move(request)]() mutable { this->sendAsyncToDispatcher(name, opt, session, resumeOpcode, std::move(req)); });
        return;
    }

    this->sendAsync(name, opt, std::move(request),
                    [this, session, resumeOpcode](Response r)
                    {
                        if (!r.ok)
                        {
                            sm_.closeAllByPolicy("connector_failed");
                            return;
                        }
                        sm_.dispatchInjected(session, resumeOpcode, std::move(r.payload));
                    });
}

void ConnectorManager::startAttempt_(RequestId id) noexcept
{
    auto it = pending_.find(id);
    if (it == pending_.end())
        return;

    auto &p = it->second;
    auto *c = find_(p.connectorName);
    if (!c)
    {
        onComplete_(id, p.attempt, Response::fail("unknown_connector"));
        return;
    }

    const int attempt = p.attempt;
    const auto timeout = p.timeoutResolved;

    loop_.addTimer(timeout, [this, id, attempt]() noexcept { this->onTimeout_(id, attempt); });

    c->send(id, attempt, p.opt, p.request);
}

void ConnectorManager::onTimeout_(RequestId id, int expectedAttempt) noexcept
{
    auto it = pending_.find(id);
    if (it == pending_.end())
        return;

    auto &p = it->second;
    if (p.attempt != expectedAttempt)
        return;

    if (p.opt.retryOnce && p.attempt == 0)
    {
        p.attempt = 1;
        startAttempt_(id);
        return;
    }

    onComplete_(id, p.attempt, Response::fail("timeout"));
}

void ConnectorManager::onComplete_(RequestId id, int attempt, Response &&r) noexcept
{
    auto it = pending_.find(id);
    if (it == pending_.end())
        return;

    auto p = std::move(it->second);
    pending_.erase(it);

    if (attempt != p.attempt)
        return;

    if (p.cb)
        p.cb(std::move(r));
}

// ===========================================================
// Outbound TCP Dial (moved from SessionManager, simplified)
// ===========================================================
void ConnectorManager::dialTcpSession(DialTcpOptions opt, DialTcpCallback cb) noexcept
{
    if (!loop_.isInOwnerThread())
    {
        loop_.post([this, opt = std::move(opt), cb = std::move(cb)]() mutable { this->dialTcpSession(std::move(opt), std::move(cb)); });
        return;
    }

    if (!cb)
        return;

    if (opt.host.empty() || opt.port == 0)
    {
        cb(false, hypernet::SessionHandle{}, "dialTcpSession: invalid host/port");
        return;
    }

    const DialId dialId = nextDialId_++;
    auto st = std::make_shared<DialState>();
    st->dialId = dialId;
    st->owner = this;
    st->loop = &loop_;
    st->opt = std::move(opt);
    st->cb = std::move(cb);
    st->attemptIndex = 0;
    st->connectedEventRegistered = false;
    st->completed = false;
    st->peer.ip = st->opt.host;
    st->peer.port = st->opt.port;

    dials_.emplace(dialId, st);

    SLOG_INFO("Dial", "Start", "dial_id={} host='{}' port={}", dialId, st->opt.host, st->opt.port);

    dialStartAttempt_(dialId, 0);
}

void ConnectorManager::dialStartAttempt_(DialId dialId, std::uint32_t attemptIndex) noexcept
{
    auto it = dials_.find(dialId);
    if (it == dials_.end() || !it->second)
        return;

    auto st = it->second;
    if (st->completed)
        return;

    if (st->sock.isValid())
    {
        if (st->connectedEventRegistered)
            (void)loop_.removeFd(st->sock.nativeHandle());
        st->sock.close();
    }
    st->connectedEventRegistered = false;

    st->attemptIndex = attemptIndex;

    hypernet::net::Socket client = hypernet::net::Socket::createTcpIPv4();
    if (!client.isValid())
    {
        finishDialFail_(dialId, attemptIndex, "socket() failed", false);
        return;
    }

    (void)client.setNonBlocking(true);
    if (st->opt.tcpNoDelay)
        (void)client.setNoDelay(true);

    if (client.connect(st->opt.host, st->opt.port))
    {
        st->sock = std::move(client);
        finishDialOk_(dialId, attemptIndex);
        return;
    }

    if (errno != EINPROGRESS)
    {
        const std::string err = std::strerror(errno);

        if (st->opt.retryOnce && attemptIndex == 0)
        {
            const auto delay = std::chrono::milliseconds(st->opt.retryDelayMs);
            loop_.addTimer(delay, [this, dialId]() noexcept { this->dialStartAttempt_(dialId, 1); });
            return;
        }

        finishDialFail_(dialId, attemptIndex, err, false);
        return;
    }

    st->sock = std::move(client);
    const int fd = st->sock.nativeHandle();

    const std::uint32_t mask = hypernet::net::EpollReactor::makeEventMask({
        hypernet::net::EpollReactor::Event::Write,
        hypernet::net::EpollReactor::Event::EdgeTriggered,
        hypernet::net::EpollReactor::Event::Error,
        hypernet::net::EpollReactor::Event::Hangup,
        hypernet::net::EpollReactor::Event::ReadHangup,
    });

    if (!loop_.addFd(fd, mask, st.get()))
    {
        st->sock.close();
        finishDialFail_(dialId, attemptIndex, "addFd failed", false);
        return;
    }
    st->connectedEventRegistered = true;

    if (st->opt.timeoutMs > 0)
    {
        loop_.addTimer(std::chrono::milliseconds(st->opt.timeoutMs), [this, dialId, attemptIndex]() noexcept { this->dialTimeout_(dialId, attemptIndex); });
    }
}

void ConnectorManager::dialTimeout_(DialId dialId, std::uint32_t expectedAttempt) noexcept
{
    auto it = dials_.find(dialId);
    if (it == dials_.end() || !it->second)
        return;

    auto st = it->second;
    if (st->completed || st->attemptIndex != expectedAttempt)
        return;

    if (st->opt.retryOnce && expectedAttempt == 0)
    {
        const auto delay = std::chrono::milliseconds(st->opt.retryDelayMs);
        loop_.addTimer(delay, [this, dialId]() noexcept { this->dialStartAttempt_(dialId, 1); });
        return;
    }

    finishDialFail_(dialId, expectedAttempt, "dial timeout", true);
}

void ConnectorManager::finishDialOk_(DialId dialId, std::uint32_t attemptIndex) noexcept
{
    auto it = dials_.find(dialId);
    if (it == dials_.end() || !it->second)
        return;

    auto st = it->second;
    if (st->completed || st->attemptIndex != attemptIndex)
        return;

    if (st->sock.isValid() && st->connectedEventRegistered)
    {
        (void)loop_.removeFd(st->sock.nativeHandle());
        st->connectedEventRegistered = false;
    }

    hypernet::net::Socket connected = std::move(st->sock);
    const auto peer = st->peer;
    st->completed = true;

    hypernet::SessionHandle h = sm_.onAccepted(std::move(connected), peer);
    if (!h)
    {
        st->cb(false, hypernet::SessionHandle{}, "dial connected but failed to create session");
        dials_.erase(it);
        return;
    }

    SLOG_INFO("Dial", "Ok", "dial_id={} sid={} peer_ip={} peer_port={}", dialId, h.id(), peer.ip, peer.port);

    st->cb(true, h, "");
    dials_.erase(it);
}

void ConnectorManager::finishDialFail_(DialId dialId, std::uint32_t attemptIndex, std::string err, bool isTimeout) noexcept
{
    auto it = dials_.find(dialId);
    if (it == dials_.end() || !it->second)
        return;

    auto st = it->second;
    if (st->completed || st->attemptIndex != attemptIndex)
        return;

    if (st->sock.isValid() && st->connectedEventRegistered)
    {
        (void)loop_.removeFd(st->sock.nativeHandle());
        st->connectedEventRegistered = false;
    }
    st->sock.close();
    st->completed = true;

    SLOG_ERROR("Dial", "Failed", "dial_id={} attempt={} timeout={} err='{}'", dialId, attemptIndex, isTimeout ? 1 : 0, err);

    st->cb(false, hypernet::SessionHandle{}, std::move(err));
    dials_.erase(it);
}

void ConnectorManager::shutdownDialsInOwnerThread() noexcept
{
    if (!loop_.isInOwnerThread())
    {
        loop_.post([this]() noexcept { this->shutdownDialsInOwnerThread(); });
        return;
    }

    for (auto &kv : dials_)
    {
        auto &st = kv.second;
        if (!st)
            continue;

        if (st->sock.isValid())
        {
            if (st->connectedEventRegistered)
                (void)loop_.removeFd(st->sock.nativeHandle());
            st->sock.close();
        }

        st->completed = true;
    }
    dials_.clear();
}

} // namespace hypernet::connector
