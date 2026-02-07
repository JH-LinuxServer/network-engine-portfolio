#include <trading/feature/handshake/exchange/RoleHelloExchangeController.hpp>

#include <trading/controllers/PacketBind.hpp>
#include <trading/protocol/FepPackets.hpp>

#include <hypernet/core/Logger.hpp>

namespace trading::feature::exchange
{
void RoleHelloExchangeController::install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime)
{
    runtime_ = &runtime;
    auto self = shared_from_this();
    BIND_PACKET(trading::protocol::RoleHelloAckPkt, RoleHelloExchangeController::onRoleHelloAck_);
}

void RoleHelloExchangeController::onSessionStart(hypernet::SessionHandle session)
{
    trading::protocol::RoleHelloReqPkt req{};
    req.role = trading::protocol::PeerRole::Exchange;

    const bool sent = runtime_->service().sendTo(session.id(), req);

    // [수정] LOG_INFO -> SLOG_INFO
    SLOG_INFO("RoleHelloExch", "ReqSent", "sent={} sid={} role={}", sent ? 1 : 0, session.id(), static_cast<int>(req.role));
}

void RoleHelloExchangeController::onRoleHelloAck_(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::RoleHelloAckPkt &pkt, const hyperapp::SessionContext &)
{
    const bool ok = (pkt.result == trading::protocol::HelloResult::Ok);

    // [수정] LOG_INFO -> SLOG_INFO
    SLOG_INFO("RoleHelloExch", "AckReceived", "ok={} sid={} assigned_role={}", ok ? 1 : 0, session.id(), static_cast<int>(pkt.role));

    if (!ok)
        return;

    rt.service().setState(session.id(), hyperapp::ConnState::Handshaked);

    if (onOk_)
        onOk_(rt, session);
}
} // namespace trading::feature::exchange
