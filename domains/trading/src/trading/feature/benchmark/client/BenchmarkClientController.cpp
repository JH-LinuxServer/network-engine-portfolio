// FILE: ./domains/trading/src/trading/feature/benchmark/client/BenchmarkClientController.cpp
#include <trading/feature/benchmark/client/BenchmarkClientController.hpp>

#include <chrono>
#include <cstdlib>   // std::exit
#include <iostream>  // std::cout
#include <algorithm> // std::max

#include <hypernet/core/Logger.hpp>
#include <trading/controllers/PacketBind.hpp>
#include <trading/protocol/FepPackets.hpp>

namespace
{
inline std::uint64_t nowNs()
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}
} // namespace

namespace trading::feature::benchmark::client
{

BenchmarkClientController::BenchmarkClientController(int target_sessions_per_worker) : recorder_(kMeasureCount), target_sessions_(std::max(1, target_sessions_per_worker)) {}

void BenchmarkClientController::install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime)
{
    (void)runtime;
    auto self = shared_from_this();

    // Handshaked 상태에서 PerfPong 수신 허용
    BIND_PACKET_WITH_STATES(trading::protocol::PerfPongPkt, BenchmarkClientController::onPerfPong, hyperapp::ConnState::Handshaked);
}

void BenchmarkClientController::onHandshakeOk(hyperapp::AppRuntime &rt, hypernet::SessionHandle session)
{

    sessions_.push_back(session.id());
    if (tpsMode_)
    {
        trading::protocol::PerfPingPkt pkt{};
        pkt.seq = 1;
        (void)rt.service().sendTo(session.id(), pkt);
    }
    else if (!started_ && static_cast<int>(sessions_.size()) >= target_sessions_)
    {
        started_ = true;
        rr_ = 0;
        seq_ = 1;

        start_ns_ = (kWarmupCount == 0) ? nowNs() : 0;

        SLOG_INFO("Loadgen", "BenchmarkStart", "Target={} (Warmup={} + Measure={}) sessions={}", kWarmupCount + kMeasureCount, kWarmupCount, kMeasureCount, sessions_.size());

        trading::protocol::PerfPingPkt pkt{};
        pkt.seq = seq_;
        pkt.t1 = nowNs();

        (void)rt.service().sendTo(sessions_[rr_], pkt);
    }
}

void BenchmarkClientController::onPerfPong(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::PerfPongPkt &pkt, const hyperapp::SessionContext &)
{
    if (tpsMode_)
    {
        trading::protocol::PerfPingPkt pkt{};
        pkt.seq++;
        (void)rt.service().sendTo(session.id(), pkt);
        if (pkt.seq == kMeasureCount / sessions_.size())
        {
            rt.onSessionEnd(session);
        }
        return;
    }
    const std::uint64_t t5 = nowNs();

    // Warmup 이후부터 기록
    if (pkt.seq > kWarmupCount)
    {
        const std::uint64_t rtt = (t5 > pkt.t1) ? (t5 - pkt.t1) : 0;
        const std::uint64_t h1 = (pkt.t2 > pkt.t1) ? (pkt.t2 - pkt.t1) : 0;
        const std::uint64_t h2 = (pkt.t3 > pkt.t2) ? (pkt.t3 - pkt.t2) : 0;
        const std::uint64_t h3 = (pkt.t4 > pkt.t3) ? (pkt.t4 - pkt.t3) : 0;
        const std::uint64_t h4 = (t5 > pkt.t4) ? (t5 - pkt.t4) : 0;
        recorder_.record(rtt, h1, h2, h3, h4);
    }

    // 측정 구간 시작점(= warmup 마지막 pong을 받은 직후) 기록
    if (pkt.seq == kWarmupCount)
    {
        start_ns_ = nowNs();
    }

    // 종료 조건: warmup+measure 모두 끝나면 종료
    if (pkt.seq >= (kWarmupCount + kMeasureCount))
    {
        const std::uint64_t end_ns = nowNs();
        const double elapsed_s = (start_ns_ > 0 && end_ns > start_ns_) ? double(end_ns - start_ns_) / 1e9 : 0.0;
        const double ops = double(kMeasureCount);
        const double ops_sec = (elapsed_s > 0.0) ? (ops / elapsed_s) : 0.0;

        recorder_.printReport();
        std::cout << "Elapsed(s): " << elapsed_s << "\n";
        std::cout << "Ops/sec   : " << ops_sec << "\n";

        SLOG_INFO("Loadgen", "BenchmarkFinish", "Done. Exiting...");
        std::exit(0);
    }

    // 다음 ping: RR로 세션을 순회
    seq_ = pkt.seq + 1;
    rr_ = (rr_ + 1) % sessions_.size();

    trading::protocol::PerfPingPkt next{};
    next.seq = seq_;
    next.t1 = nowNs();

    (void)rt.service().sendTo(sessions_[rr_], next);
}

} // namespace trading::feature::benchmark::client
