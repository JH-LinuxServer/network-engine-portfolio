#pragma once

#include <hyperapp/core/AppRuntime.hpp>
#include <hypernet/SessionHandle.hpp>
#include <trading/controllers/IController.hpp>
#include <trading/protocol/FepPackets.hpp>

namespace hyperapp
{
struct SessionContext;
} // namespace hyperapp

namespace trading::feature::benchmark::exchange
{

class BenchmarkExchangeController final : public trading::IController, public std::enable_shared_from_this<BenchmarkExchangeController>
{
  public:
    BenchmarkExchangeController() = default;

    void install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime) override;

    // [핵심] Ping 수신 -> Pong 반송
    void onPerfPing(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::PerfPingPkt &pkt, const hyperapp::SessionContext &ctx);

  private:
};

} // namespace trading::feature::benchmark::exchange
