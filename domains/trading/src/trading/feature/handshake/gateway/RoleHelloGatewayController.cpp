#include <trading/feature/handshake/gateway/RoleHelloGatewayController.hpp>

#include <hypernet/core/Logger.hpp>
#include <trading/controllers/PacketBind.hpp>

namespace trading::feature::gateway
{
namespace
{
// [FIX] client_sid 인자 제거 (패킷 구조체에 없음)
inline trading::protocol::RoleHelloAckPkt buildAck(trading::protocol::HelloResult result, trading::protocol::PeerRole assignedRole)
{
    trading::protocol::RoleHelloAckPkt ack{};
    ack.result = result;
    ack.role = assignedRole;
    return ack;
}
} // namespace

// [FIX] 생성자 구현
RoleHelloGatewayController::RoleHelloGatewayController(std::shared_ptr<hyperapp::UpstreamGateway> upstream) : upstream_(std::move(upstream)) {}

void RoleHelloGatewayController::install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime)
{
    (void)runtime;
    auto self = shared_from_this();
    BIND_PACKET(trading::protocol::RoleHelloReqPkt, RoleHelloGatewayController::onRoleHello_);
}

void RoleHelloGatewayController::onRoleHello_(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::RoleHelloReqPkt &pkt, const hyperapp::SessionContext &)
{
    const int wid = hypernet::core::wid();

    // [FIX] 메서드 이름 수정: getUpstreamId -> getForWorker
    // FepGatewayApplication.cpp에서 setForWorker를 사용하므로 짝을 맞춤
    const auto localUp = upstream_ ? upstream_->getForWorker(wid) : 0;

    // [FIX] PeerRole::Upstream -> PeerRole::Exchange (Enum 정의에 Upstream 없음)
    if (pkt.role == trading::protocol::PeerRole::Exchange)
    {
        if (localUp != 0 && session.id() != localUp)
        {
            rt.service().close(session.id(), std::string("UPSTREAM_ROLE_CONFLICT"));
            return;
        }

        // [FIX] setUpstreamId -> setForWorker
        if (upstream_ && localUp == 0)
            upstream_->setForWorker(wid, session.id());
    }
    else
    {
        // 일반 클라이언트가 업스트림 세션 ID와 같다면 오류
        if (localUp != 0 && session.id() == localUp)
        {
            rt.service().close(session.id(), std::string("CLIENT_SID_EQUALS_UPSTREAM"));
            return;
        }
    }

    // 정상 Ack 반환
    rt.service().setState(session.id(), hyperapp::ConnState::Handshaked);

    trading::protocol::PeerRole assigned = pkt.role;
    trading::protocol::HelloResult result = trading::protocol::HelloResult::Ok;

    // [FIX] client_sid 설정 제거
    auto ack = buildAck(result, assigned);
    const bool sent = rt.service().sendTo(session.id(), ack);

    SLOG_INFO("RoleHelloGateway", "ReqRecv", "sid={} role={} localUp={} wid={} sent={}", session.id(), static_cast<int>(pkt.role), localUp, wid, sent ? 1 : 0);
}

void RoleHelloGatewayController::onSessionEnd(hypernet::SessionHandle session)
{
    const int wid = hypernet::core::wid();
    if (!upstream_)
        return;

    // [FIX] getUpstreamId -> getForWorker
    const auto upId = upstream_->getForWorker(wid);
    if (upId != 0 && upId == session.id())
    {
        // [FIX] setUpstreamId -> setForWorker
        upstream_->setForWorker(wid, 0);
        SLOG_INFO("RoleHelloGateway", "UpstreamCleared", "wid={} sid={}", wid, session.id());
    }
}
} // namespace trading::feature::gateway