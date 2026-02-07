#include <trading/feature/handshake/client/RoleHelloClientController.hpp>

#include <trading/controllers/PacketBind.hpp>
#include <trading/protocol/FepPackets.hpp>

#include <hypernet/core/Logger.hpp>

namespace trading::feature::client
{
void RoleHelloClientController::install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime)
{
    runtime_ = &runtime;
    auto self = shared_from_this();
    BIND_PACKET(trading::protocol::RoleHelloAckPkt, RoleHelloClientController::onRoleHelloAck_);
}
// 모든 컨트롤러에 대해서 세션이 연결되면 onSessionStart 가 호출된다 오버라이드 해서 구현했다면
void RoleHelloClientController::onSessionStart(hypernet::SessionHandle session)
{
    trading::protocol::RoleHelloReqPkt req{};
    req.role = trading::protocol::PeerRole::Client;

    const bool sent = runtime_->service().sendTo(session.id(), req);
    SLOG_INFO("RoleHelloClient", "ReqSent", "sent={} sid={} role={}", sent ? 1 : 0, session.id(), static_cast<int>(req.role));
}

void RoleHelloClientController::onRoleHelloAck_(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::RoleHelloAckPkt &pkt, const hyperapp::SessionContext &)
{
    const bool ok = (pkt.result == trading::protocol::HelloResult::Ok);

    // [수정] LOG_INFO -> SLOG_INFO
    SLOG_INFO("RoleHelloClient", "AckReceived", "ok={} sid={} assigned_role={}", ok ? 1 : 0, session.id(), static_cast<int>(pkt.role));

    if (!ok)
        return;

    rt.service().setState(session.id(), hyperapp::ConnState::Handshaked);

    if (onOk_)
        onOk_(rt, session);
}
} // namespace trading::feature::client
