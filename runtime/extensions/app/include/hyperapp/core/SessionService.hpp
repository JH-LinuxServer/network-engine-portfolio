#pragma once

#include <hyperapp/core/SessionContext.hpp>
#include <hyperapp/core/SessionRegistry.hpp>
#include <hyperapp/core/TopicBroadcaster.hpp>
#include <hyperapp/protocol/PacketWriter.hpp>

#include <hypernet/ISessionRouter.hpp>
#include <hypernet/IWorkerScheduler.hpp>
#include <hypernet/SessionHandle.hpp>
#include <hypernet/protocol/MessageView.hpp>
#include <hypernet/connector/ConnectorManager.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector> // [NEW]

namespace hyperapp::detail
{
template <typename Packet>
concept OutboundPacket = requires(hyperapp::protocol::PacketWriter &w, const Packet &pkt) {
    { Packet::kOpcode } -> std::convertible_to<std::uint16_t>;
    { pkt.write(w) };
};

template <typename Packet>
inline constexpr bool kWriteReturnsBool = requires(hyperapp::protocol::PacketWriter &w, const Packet &pkt) {
    { pkt.write(w) } -> std::convertible_to<bool>;
};

template <typename Packet>
inline constexpr bool kHasReserveBytes = requires {
    { Packet::kReserveBytes } -> std::convertible_to<std::size_t>;
};

template <OutboundPacket Packet> std::optional<std::size_t> reserveHint(const Packet & /*pkt*/) noexcept
{
    if constexpr (kHasReserveBytes<Packet>)
        return static_cast<std::size_t>(Packet::kReserveBytes);
    return std::nullopt;
}

template <OutboundPacket Packet> bool writePacket(const Packet &pkt, hyperapp::protocol::PacketWriter &w) noexcept
{
    if constexpr (kWriteReturnsBool<Packet>)
        return static_cast<bool>(pkt.write(w));
    else
    {
        pkt.write(w);
        return true;
    }
}
} // namespace hyperapp::detail

namespace hyperapp
{
namespace core
{
struct ConnectTcpResult
{
    bool ok{false};
    hypernet::SessionHandle session{};
    std::optional<std::string> err{};
};
} // namespace core

struct ConnectTcpOptions
{
    std::string host{};
    std::uint16_t port{0};
    ScopeId targetScope{0};
    TopicId targetTopic{0};
};

using ConnectTcpCallback = std::function<void(core::ConnectTcpResult res)>;

class SessionService final
{
  public:
    using SessionId = hypernet::SessionHandle::Id;

    SessionService(int ownerWorkerId, SessionRegistry &reg, TopicBroadcaster &bc) noexcept : ownerWorkerId_(ownerWorkerId), reg_(reg), bc_(bc) {}

    void setRouter(std::shared_ptr<hypernet::ISessionRouter> router) noexcept { router_ = std::move(router); }
    void setScheduler(std::shared_ptr<hypernet::IWorkerScheduler> scheduler) noexcept { scheduler_ = std::move(scheduler); }

    // ---------------------------------------------------------------------
    // Connect
    // ---------------------------------------------------------------------
    void connectTcp(ConnectTcpOptions opt, ConnectTcpCallback cb) noexcept;

    // ---------------------------------------------------------------------
    // 송신 창구
    // ---------------------------------------------------------------------
    template <detail::OutboundPacket Packet> [[nodiscard]] bool sendTo(SessionId sid, const Packet &pkt) noexcept
    {
        if (!router_)
            return false;

        hyperapp::protocol::PacketWriter w;
        if (auto rh = detail::reserveHint(pkt); rh)
            w.reserve(*rh);

        if (!detail::writePacket(pkt, w))
            return false;

        return sendTo(sid, Packet::kOpcode, w.view());
    }

    // Local-only APIs
    template <detail::OutboundPacket Packet> [[nodiscard]] bool sendToLocal(SessionId sid, const Packet &pkt) noexcept
    {
        if (!router_)
            return false;

        hyperapp::protocol::PacketWriter w;
        if (auto rh = detail::reserveHint(pkt); rh)
            w.reserve(*rh);

        if (!detail::writePacket(pkt, w))
            return false;

        return sendToLocal(sid, Packet::kOpcode, w.view());
    }

    [[nodiscard]] bool sendToLocal(SessionId sid, std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept { return sendToLocal_(sid, opcode, body); }

    // ---------------------------------------------------------------------
    // Broadcast / Multicast (도메인 최종 표면)
    // ---------------------------------------------------------------------
    template <detail::OutboundPacket Packet> void broadcastTopic(ScopeId w, TopicId c, const Packet &pkt, SessionId exceptSid = 0) noexcept
    {
        if (!router_)
            return;

        hyperapp::protocol::PacketWriter pw;
        if (auto rh = detail::reserveHint(pkt); rh)
            pw.reserve(*rh);

        if (!detail::writePacket(pkt, pw))
            return;

        bc_.broadcastTopic(w, c, Packet::kOpcode, pw.view(), exceptSid);
    }

    // [NEW] scope 브로드캐스트
    template <detail::OutboundPacket Packet> void broadcastScope(ScopeId w, const Packet &pkt, SessionId exceptSid = 0) noexcept
    {
        if (!router_)
            return;

        hyperapp::protocol::PacketWriter pw;
        if (auto rh = detail::reserveHint(pkt); rh)
            pw.reserve(*rh);

        if (!detail::writePacket(pkt, pw))
            return;

        bc_.broadcastScope(w, Packet::kOpcode, pw.view(), exceptSid);
    }

    // [NEW] 전체 브로드캐스트
    template <detail::OutboundPacket Packet> void broadcastAll(const Packet &pkt, SessionId exceptSid = 0) noexcept
    {
        if (!router_)
            return;

        hyperapp::protocol::PacketWriter pw;
        if (auto rh = detail::reserveHint(pkt); rh)
            pw.reserve(*rh);

        if (!detail::writePacket(pkt, pw))
            return;

        bc_.broadcastAll(Packet::kOpcode, pw.view(), exceptSid);
    }

    // [NEW] 임의 sid 리스트 멀티캐스트
    template <detail::OutboundPacket Packet> void multicast(const std::vector<SessionId> &sids, const Packet &pkt, SessionId exceptSid = 0) noexcept
    {
        if (!router_)
            return;

        hyperapp::protocol::PacketWriter pw;
        if (auto rh = detail::reserveHint(pkt); rh)
            pw.reserve(*rh);

        if (!detail::writePacket(pkt, pw))
            return;

        bc_.multicast(sids, Packet::kOpcode, pw.view(), exceptSid);
    }

    // ---------------------------------------------------------------------
    // Close
    // ---------------------------------------------------------------------
    void close(SessionId sid, std::string_view reason, int err = 0) noexcept { close(sid, std::string(reason), err); }
    void close(SessionId sid, std::string reason, int err = 0) noexcept;

    void closeLocal(SessionId sid, std::string_view reason, int err = 0) noexcept { closeLocal(sid, std::string(reason), err); }
    void closeLocal(SessionId sid, std::string reason, int err = 0) noexcept;

    // ---------------------------------------------------------------------
    // Subscription / Primary
    // ---------------------------------------------------------------------
    [[nodiscard]] bool subscribe(SessionId sid, ScopeId w, TopicId c) noexcept { return reg_.subscribe(sid, w, c); }
    [[nodiscard]] bool unsubscribe(SessionId sid, ScopeId w, TopicId c) noexcept { return reg_.unsubscribe(sid, w, c); }
    [[nodiscard]] bool unsubscribeAll(SessionId sid) noexcept { return reg_.unsubscribeAll(sid); }
    [[nodiscard]] bool setPrimaryTopic(SessionId sid, ScopeId w, TopicId c) noexcept { return reg_.setPrimaryTopic(sid, w, c); }

    // ---------------------------------------------------------------------
    // State / Auth helpers
    // ---------------------------------------------------------------------
    void setState(SessionId sid, ConnState st) noexcept { reg_.setState(sid, st); }
    void setAuth(SessionId sid, AccountId aid, PlayerId pid) noexcept { reg_.setAuth(sid, aid, pid); }

  private:
    [[nodiscard]] bool sendTo(SessionId sid, std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept;

    [[nodiscard]] bool sendToLocal_(SessionId sid, std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept;
    void closeLocal_(SessionId sid, std::string reason, int err) noexcept;

    void broadcastTopic(ScopeId w, TopicId c, std::uint16_t opcode, const hypernet::protocol::MessageView &body, SessionId exceptSid = 0) noexcept;

  private:
    int ownerWorkerId_{0};
    SessionRegistry &reg_;
    TopicBroadcaster &bc_;
    std::shared_ptr<hypernet::ISessionRouter> router_{};
    std::shared_ptr<hypernet::IWorkerScheduler> scheduler_{};
};
} // namespace hyperapp
