#include <trading/feature/benchmark/exchange/BenchmarkExchangeController.hpp>

#include <chrono>
#include <hypernet/core/Logger.hpp>
#include <hyperapp/protocol/PacketWriter.hpp>
#include <trading/controllers/PacketBind.hpp>
#include <trading/protocol/FepPackets.hpp>

namespace
{
// 현재 시간 (나노초)
inline std::uint64_t nowNs()
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}
} // namespace

namespace trading::feature::benchmark::exchange
{

void BenchmarkExchangeController::install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime)
{
    (void)runtime;
    auto self = shared_from_this();

    // Ping(20050) 수신 핸들러 등록
    // (Handshaked 상태 이상이면 수신 가능)
    BIND_PACKET_WITH_STATES(trading::protocol::PerfPingPkt, BenchmarkExchangeController::onPerfPing, hyperapp::ConnState::Handshaked);
}

void BenchmarkExchangeController::onPerfPing(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::PerfPingPkt &pkt, const hyperapp::SessionContext &)
{

    // 1. 응답용 Pong 패킷 생성
    trading::protocol::PerfPongPkt response;
    response.seq = pkt.seq;
    response.client_sid = pkt.client_sid; // gateway가 채워줌

    // 2. 시간 기록
    response.t1 = pkt.t1;
    response.t2 = pkt.t2;

    // Mock 도착 시간 기록 [T3]
    response.t3 = nowNs();
    (void)rt.service().sendTo(session.id(), response);
}

} // namespace trading::feature::benchmark::exchange
