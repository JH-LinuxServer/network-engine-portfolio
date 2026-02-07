// FILE: ./domains/trading/include/trading/feature/benchmark/client/BenchmarkClientController.hpp
#pragma once

#include <hyperapp/core/AppRuntime.hpp>
#include <hypernet/SessionHandle.hpp>
#include <trading/controllers/IController.hpp>

#include <trading/protocol/FepPackets.hpp>
#include <trading/feature/benchmark/client/LatencyRecorder.hpp>

#include <cstdint>
#include <cstddef>
#include <vector>

namespace hyperapp
{
struct SessionContext;
} // namespace hyperapp

namespace trading::feature::benchmark::client
{

class BenchmarkClientController final : public trading::IController, public std::enable_shared_from_this<BenchmarkClientController>
{
  public:
    explicit BenchmarkClientController(int target_sessions_per_worker);

    void install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime) override;

    // RoleHello 완료 시 호출 (세션을 모으고, 목표치 충족 시 시작)
    void onHandshakeOk(hyperapp::AppRuntime &rt, hypernet::SessionHandle session);

    // Pong 수신 핸들러
    void onPerfPong(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::PerfPongPkt &pkt, const hyperapp::SessionContext &ctx);

  private:
    // 통계 처리기
    LatencyRecorder recorder_;

    // 세션 N개 모으기
    int target_sessions_{1};
    std::vector<hypernet::SessionHandle::Id> sessions_{};
    std::size_t rr_{0};
    bool started_{false};

    // global seq / 측정 시작 타임
    std::uint64_t seq_{0};
    std::uint64_t start_ns_{0}; // 측정(Measure) 시작점

    static constexpr std::uint64_t kWarmupCount = 10000;
    static constexpr std::uint64_t kMeasureCount = 200000;
};

} // namespace trading::feature::benchmark::client
