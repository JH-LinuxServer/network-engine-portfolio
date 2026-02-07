#pragma once

#include <hypernet/ISessionSender.hpp>
#include <hypernet/SessionHandle.hpp>
#include <hypernet/net/Acceptor.hpp>
#include <hypernet/net/Session.hpp>
#include <hypernet/util/NonCopyable.hpp>
#include <hypernet/protocol/LengthPrefixFramer.hpp>
#include <hypernet/protocol/MessageView.hpp>
#include <hypernet/protocol/Dispatcher.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hypernet
{
class IApplication; // forward
}

namespace hypernet::connector
{
class ConnectorManager; // forward
}

namespace hypernet::net
{

class EventLoop;

class SessionManager final : private hypernet::util::NonCopyable
{
  public:
    SessionManager(unsigned int ownerWorkerId, EventLoop *loop, std::size_t recvRingCapacity, std::size_t sendRingCapacity, std::uint32_t framerMaxPayloadLen) noexcept;
    ~SessionManager();

    void setApplication(std::shared_ptr<hypernet::IApplication> app) noexcept;

    // Inbound accept + Outbound dial 승격 경로에서 공통으로 사용
    // (Outbound dial은 ConnectorManager가 담당하고, 연결 완료 후 여기로 승격시킨다)
    SessionHandle onAccepted(Socket &&client, const Acceptor::PeerEndpoint &peer) noexcept;

    void onSessionClosed(SessionHandle::Id id) noexcept;
    void shutdownInOwnerThread() noexcept;

    hypernet::connector::ConnectorManager &connectors() noexcept;

    void dispatchInjected(SessionHandle session, hypernet::protocol::Dispatcher::OpCode opcode, std::vector<std::uint8_t> body) noexcept;

    [[nodiscard]] std::size_t sessionCount() const noexcept { return sessions_.size(); }

    [[nodiscard]] hypernet::protocol::IFramer &framer() noexcept { return framer_; }
    [[nodiscard]] const char *lastFramerErrorReason() const noexcept { return framer_.lastErrorReason(); }

    void dispatchOnMessage(SessionHandle session, const hypernet::protocol::MessageView &message) noexcept;

    void configureTimeouts(std::uint32_t idleTimeoutMs, std::uint32_t heartbeatIntervalMs) noexcept;

    bool sendPacketU16(SessionHandle::Id id, std::uint16_t opcode, const void *body, std::size_t bodyLen) noexcept;
    void beginClose(SessionHandle::Id id, const char *reason, int err = 0) noexcept;
    void closeAllByPolicy(const char *reason, int err = 0) noexcept;

  private:
    [[nodiscard]] SessionHandle::Id nextSessionId_() noexcept;
    void assertInOwnerThread_(const char *apiName) const noexcept;

    [[nodiscard]] SessionHandle makeHandle_(SessionHandle::Id id) const noexcept;
    void closeByPolicy_(SessionHandle::Id id, const char *reason, int err = 0) noexcept;

    unsigned int ownerWorkerId_{0};
    EventLoop *loop_{nullptr};

    std::size_t recvRingCapacity_{0};
    std::size_t sendRingCapacity_{0};

    std::uint64_t localCounter_{1};

    std::uint32_t idleTimeoutMs_{0};
    std::uint32_t heartbeatIntervalMs_{0};

    std::shared_ptr<hypernet::IApplication> app_;

    hypernet::protocol::LengthPrefixFramer framer_{};

    std::shared_ptr<hypernet::ISessionSender> sender_;

    hypernet::protocol::Dispatcher dispatcher_{};
    std::unique_ptr<hypernet::connector::ConnectorManager> connectors_;

    std::unordered_map<SessionHandle::Id, std::shared_ptr<Session>> sessions_;
};

} // namespace hypernet::net
