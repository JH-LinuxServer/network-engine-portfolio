#include <trading/feature/benchmark/gateway/BenchmarkGatewayController.hpp>

#include <trading/controllers/PacketBind.hpp>
#include <trading/protocol/FepPackets.hpp>

#include <hypernet/core/ThreadContext.hpp>

#include <chrono>

namespace
{
inline std::uint64_t nowNs()
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}
} // namespace

namespace trading::feature::benchmark::gateway
{

// [FIX] 생성자 구현
BenchmarkGatewayController::BenchmarkGatewayController(std::shared_ptr<hyperapp::UpstreamGateway> upstream, bool handoff_mode)
    : upstream_(std::move(upstream)), handoff_mode_(handoff_mode)
{
}

void BenchmarkGatewayController::install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime)
{
    (void)runtime;
    auto self = shared_from_this();
    BIND_PACKET_WITH_STATES(trading::protocol::PerfPingPkt, BenchmarkGatewayController::onPerfPing, hyperapp::ConnState::Handshaked);
    BIND_PACKET_WITH_STATES(trading::protocol::PerfPongPkt, BenchmarkGatewayController::onPerfPong, hyperapp::ConnState::Handshaked);
}

void BenchmarkGatewayController::onPerfPing(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::PerfPingPkt &pkt, const hyperapp::SessionContext &)
{
    if (!upstream_ || upstream_->workerCount() <= 0)
        return;

    trading::protocol::PerfPingPkt fwd = pkt;
    fwd.client_sid = session.id();

    const int wc = upstream_->workerCount();

    // S2(local): route by current worker id (no cross-worker)
    // S3(handoff): route by client session id (cross-worker handoff)
    const int idx = handoff_mode_
                        ? static_cast<int>(session.id() % wc)
                        : static_cast<int>(hypernet::core::wid() % wc);

    const auto upstreamSid = upstream_->getForWorker(idx);

    if (upstreamSid == 0)
        return;

    fwd.t2 = nowNs();

    (void)rt.service().sendTo(upstreamSid, fwd);
}

void BenchmarkGatewayController::onPerfPong(hyperapp::AppRuntime &rt, hypernet::SessionHandle /*session*/, const trading::protocol::PerfPongPkt &pkt, const hyperapp::SessionContext &)
{
    const auto clientSid = static_cast<hypernet::SessionHandle::Id>(pkt.client_sid);
    if (clientSid == 0)
        return;

    trading::protocol::PerfPongPkt fwd = pkt;
    fwd.t4 = nowNs();

    (void)rt.service().sendTo(clientSid, fwd);
}

} // namespace trading::feature::benchmark::gateway

