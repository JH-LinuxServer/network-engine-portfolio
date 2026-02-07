#pragma once

#include <hyperapp/core/AppRuntime.hpp>
#include <hyperapp/core/UpstreamGateway.hpp>
#include <hypernet/SessionHandle.hpp>
#include <trading/controllers/IController.hpp>
#include <trading/protocol/FepPackets.hpp>

#include <memory>

namespace hyperapp
{
struct SessionContext;
} // namespace hyperapp

namespace trading::feature::benchmark::gateway
{
class BenchmarkGatewayController final : public trading::IController, public std::enable_shared_from_this<BenchmarkGatewayController>
{
  public:
    // [FIX] explicit 생성자 추가
    explicit BenchmarkGatewayController(std::shared_ptr<hyperapp::UpstreamGateway> upstream, bool handoff_mode = false);

    void install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime) override;

    void onPerfPing(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::PerfPingPkt &pkt, const hyperapp::SessionContext &ctx);
    void onPerfPong(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::PerfPongPkt &pkt, const hyperapp::SessionContext &ctx);

  private:
    std::shared_ptr<hyperapp::UpstreamGateway> upstream_;

    bool handoff_mode_{false};
};
} // namespace trading::feature::benchmark::gateway

