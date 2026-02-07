#pragma once

#include <trading/controllers/IController.hpp>

#include <functional>
#include <memory>

namespace trading::protocol
{
struct RoleHelloAckPkt;
} // namespace trading::protocol

namespace hyperapp
{
struct SessionContext;
} // namespace hyperapp

namespace trading::feature::client
{
class RoleHelloClientController final : public trading::IController, public std::enable_shared_from_this<RoleHelloClientController>
{
  public:
    using OnHandshakeOk = std::function<void(hyperapp::AppRuntime &, hypernet::SessionHandle)>;
    // [FIX] 기본 생성자 허용, setOnOk로 콜백 주입
    RoleHelloClientController() = default;

    void setOnOk(OnHandshakeOk onOk) { onOk_ = std::move(onOk); }

    void install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime) override;
    void onSessionStart(hypernet::SessionHandle session) override;

  private:
    void onRoleHelloAck_(hyperapp::AppRuntime &rt, hypernet::SessionHandle session, const trading::protocol::RoleHelloAckPkt &pkt, const hyperapp::SessionContext &ctx);
    hyperapp::AppRuntime *runtime_{nullptr};
    OnHandshakeOk onOk_;
};
} // namespace trading::feature::client
