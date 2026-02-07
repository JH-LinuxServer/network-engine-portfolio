#include <hypernet/net/SessionManager.hpp>

#include <hypernet/IApplication.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/net/EventLoop.hpp>
#include <hypernet/net/EpollReactor.hpp>
#include <hypernet/monitoring/Metrics.hpp>
#include <hypernet/protocol/BuiltinOpcodes.hpp>
#include <hypernet/protocol/Endian.hpp>
#include <hypernet/protocol/OpcodeU16.hpp>
#include <hypernet/connector/ConnectorManager.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hypernet::net
{

namespace
{

inline int wid() noexcept
{
    return hypernet::core::ThreadContext::currentWorkerId();
}

/// per-worker sender 구현
class PerWorkerSessionSender final : public hypernet::ISessionSender
{
  public:
    PerWorkerSessionSender(unsigned int ownerWorkerId, SessionManager *owner) noexcept : ownerWorkerId_(ownerWorkerId), owner_(owner) {}

    bool sendPacketU16(hypernet::ISessionSender::SessionId sessionId, std::uint16_t opcode, const void *body, std::size_t bodyLen) noexcept override
    {
        if (!owner_)
            return false;

        if (hypernet::core::ThreadContext::currentWorkerId() != static_cast<int>(ownerWorkerId_))
        {
            SLOG_ERROR("SessionSender", "CrossThreadSendBlocked", "expected_owner_w={} session_id={}", ownerWorkerId_, sessionId);
            return false;
        }
        return owner_->sendPacketU16(static_cast<SessionHandle::Id>(sessionId), opcode, body, bodyLen);
    }

  private:
    unsigned int ownerWorkerId_{0};
    SessionManager *owner_{nullptr};
};

} // namespace

// ===== SessionManager =====

SessionManager::SessionManager(unsigned int ownerWorkerId, EventLoop *loop, std::size_t recvRingCapacity, std::size_t sendRingCapacity, std::uint32_t framerMaxPayloadLen) noexcept
    : ownerWorkerId_(ownerWorkerId), loop_(loop), recvRingCapacity_(recvRingCapacity), sendRingCapacity_(sendRingCapacity), framer_(framerMaxPayloadLen)
{
    sender_ = std::make_shared<PerWorkerSessionSender>(ownerWorkerId_, this);
    if (loop_)
    {
        connectors_ = std::make_unique<hypernet::connector::ConnectorManager>(*loop_, *this);
    }
}

SessionManager::~SessionManager()
{
    if (connectors_ && loop_ && loop_->isInOwnerThread())
    {
        connectors_->shutdownDialsInOwnerThread();
    }

    sessions_.clear();
    sender_.reset();
    connectors_.reset();
}

void SessionManager::setApplication(std::shared_ptr<hypernet::IApplication> app) noexcept
{
    app_ = std::move(app);
    dispatcher_.clear();

    if (!app_)
        return;

    app_->registerHandlers(dispatcher_);
    SLOG_INFO("SessionManager", "DispatcherReady", "handlers={}", dispatcher_.handlerCount());
}

SessionHandle SessionManager::makeHandle_(SessionHandle::Id id) const noexcept
{
    return SessionHandle{id, static_cast<int>(ownerWorkerId_), std::weak_ptr<hypernet::ISessionSender>(sender_)};
}

void SessionManager::assertInOwnerThread_(const char *apiName) const noexcept
{
    const int cw = wid();
    if (cw != static_cast<int>(ownerWorkerId_))
    {
        SLOG_FATAL("SessionManager", "WrongWorker", "api='{}' expected_w={} current_w={}", apiName ? apiName : "(unknown)", ownerWorkerId_, cw);
        std::abort();
    }
}

SessionHandle::Id SessionManager::nextSessionId_() noexcept
{
    return static_cast<SessionHandle::Id>((static_cast<std::uint64_t>(ownerWorkerId_) << 32) | (localCounter_++));
}

// ===== Inbound accept / Outbound dial upgrade -> Session =====
SessionHandle SessionManager::onAccepted(Socket &&client, const Acceptor::PeerEndpoint &peer) noexcept
{
    assertInOwnerThread_("onAccepted");
    if (!loop_)
        std::abort();

    const auto id = nextSessionId_();
    auto handle = makeHandle_(id);

    auto session = Session::create(handle, static_cast<int>(ownerWorkerId_), std::move(client), this, recvRingCapacity_, sendRingCapacity_);

    if (!session)
    {
        hypernet::monitoring::engineMetrics().onError();
        return SessionHandle{};
    }

    const std::uint32_t mask = EpollReactor::makeEventMask({
        EpollReactor::Event::Read,
        EpollReactor::Event::EdgeTriggered,
        EpollReactor::Event::Error,
        EpollReactor::Event::Hangup,
        EpollReactor::Event::ReadHangup,
    });

    const int fd = session->nativeHandle();
    if (!loop_->addFd(fd, mask, session.get()))
    {
        hypernet::monitoring::engineMetrics().onError();
        return SessionHandle{};
    }

    sessions_.emplace(id, session);
    session->startTimeouts_(*loop_, idleTimeoutMs_, heartbeatIntervalMs_);
    hypernet::monitoring::engineMetrics().onConnectionOpened();

    SLOG_INFO("SessionManager", "SessionStart", "sid={} fd={} peer_ip={} peer_port={}", id, fd, peer.ip, peer.port);

    if (app_)
    {
        try
        {
            app_->onSessionStart(handle);
        }
        catch (...)
        {
            SLOG_ERROR("SessionManager", "OnSessionStartThrew", "");
        }
    }

    return handle;
}

hypernet::connector::ConnectorManager &SessionManager::connectors() noexcept
{
    assertInOwnerThread_("connectors");
    return *connectors_;
}

void SessionManager::dispatchInjected(SessionHandle session, hypernet::protocol::Dispatcher::OpCode opcode, std::vector<std::uint8_t> body) noexcept
{
    assertInOwnerThread_("dispatchInjected");

    if (sessions_.find(session.id()) == sessions_.end())
        return;

    hypernet::protocol::MessageView view(body.empty() ? nullptr : body.data(), body.size());
    const bool handled = dispatcher_.dispatch(opcode, session, view);
    if (!handled)
    {
        closeByPolicy_(session.id(), "unknown_injected_opcode");
    }
}

void SessionManager::onSessionClosed(SessionHandle::Id id) noexcept
{
    assertInOwnerThread_("onSessionClosed");

    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return;

    auto session = it->second;
    SessionHandle handle = session ? session->handle() : makeHandle_(id);

    sessions_.erase(it);
    hypernet::monitoring::engineMetrics().onConnectionClosed();
    SLOG_INFO("SessionManager", "SessionEnd", "sid={}", id);

    if (app_)
    {
        try
        {
            app_->onSessionEnd(handle);
        }
        catch (...)
        {
            SLOG_ERROR("SessionManager", "OnSessionEndThrew", "");
        }
    }
}

void SessionManager::shutdownInOwnerThread() noexcept
{
    assertInOwnerThread_("shutdownInOwnerThread");
    if (!loop_)
        return;

    if (connectors_)
    {
        connectors_->shutdownDialsInOwnerThread();
    }

    for (auto &kv : sessions_)
    {
        auto &s = kv.second;
        if (s)
            s->closeFromManager_(*loop_, "worker_shutdown");
    }
    sessions_.clear();
}

void SessionManager::closeByPolicy_(hypernet::SessionHandle::Id id, const char *reason, int err) noexcept
{
    assertInOwnerThread_("closeByPolicy");
    if (!loop_)
        return;

    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return;

    if (auto &sess = it->second)
        sess->beginClose_(*loop_, reason ? reason : "policy_close", err);
}

void SessionManager::closeAllByPolicy(const char *reason, int err) noexcept
{
    assertInOwnerThread_("closeAllByPolicy");
    std::vector<hypernet::SessionHandle::Id> ids;
    ids.reserve(sessions_.size());
    for (auto &kv : sessions_)
        ids.push_back(kv.first);

    for (auto id : ids)
        closeByPolicy_(id, reason, err);
}

void SessionManager::dispatchOnMessage(SessionHandle session, const hypernet::protocol::MessageView &message) noexcept
{
    assertInOwnerThread_("dispatchOnMessage");
    hypernet::monitoring::engineMetrics().onRxMessage();

    std::uint16_t opcode = 0;
    hypernet::protocol::MessageView body{};

    if (!hypernet::protocol::splitOpcodeU16Be(message, opcode, body))
    {
        closeByPolicy_(session.id(), "invalid_opcode_prefix");
        return;
    }

    if (opcode == hypernet::protocol::kOpcodePing)
    {
        (void)sendPacketU16(session.id(), hypernet::protocol::kOpcodePong, nullptr, 0);
        return;
    }
    if (opcode == hypernet::protocol::kOpcodePong)
        return;

    if (!dispatcher_.dispatch(opcode, session, body))
    {
        closeByPolicy_(session.id(), "unknown_opcode");
    }
}

void SessionManager::configureTimeouts(std::uint32_t idleTimeoutMs, std::uint32_t heartbeatIntervalMs) noexcept
{
    assertInOwnerThread_("configureTimeouts");
    idleTimeoutMs_ = idleTimeoutMs;
    heartbeatIntervalMs_ = heartbeatIntervalMs;
}

bool SessionManager::sendPacketU16(SessionHandle::Id id, std::uint16_t opcode, const void *body, std::size_t bodyLen) noexcept
{
    assertInOwnerThread_("sendPacketU16");

    const std::size_t payloadLen = hypernet::protocol::MessageHeader::payloadLenForBody(bodyLen);
    if (payloadLen > hypernet::protocol::MessageHeader::kMaxPayloadLenU64)
        return false;

    auto it = sessions_.find(id);
    if (it == sessions_.end() || !it->second)
        return false;

    const hypernet::protocol::MessageHeader hdr{
        static_cast<std::uint32_t>(payloadLen),
        opcode,
    };

    std::uint8_t lenHdr[hypernet::protocol::MessageHeader::kLengthFieldBytes];
    hdr.encodeLen(lenHdr);

    std::uint8_t opHdr[hypernet::protocol::MessageHeader::kOpcodeFieldBytes];
    hdr.encodeOpcode(opHdr);

    return it->second->enqueuePacketU16Coalesced(*loop_, lenHdr, opHdr, body, bodyLen);
}

void SessionManager::beginClose(SessionHandle::Id id, const char *reason, int err) noexcept
{
    // owner 워커에서만 호출되도록 설계 (SessionService가 postToWorker로 보장)
    closeByPolicy_(id, reason, err);
}

} // namespace hypernet::net
