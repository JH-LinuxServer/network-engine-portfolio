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

namespace trading::feature::gateway
{
class RoleHelloGatewayController final : public trading::IController, public std::enable_shared_from_this<RoleHelloGatewayController>
{
  public:
    // [FIX] 생성자 인자 추가 (Install.cpp와 일치시킴)
    explicit RoleHelloGatewayController(std::shared_ptr<hyperapp::UpstreamGateway> upstream);

    void install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime) override;
    void onSessionEnd(hypernet::SessionHandle session) override;

  private:
    void onRoleHello_(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::RoleHelloReqPkt &pkt, const hyperapp::SessionContext &ctx);

    std::shared_ptr<hyperapp::UpstreamGateway> upstream_;
};
} // namespace trading::feature::gateway