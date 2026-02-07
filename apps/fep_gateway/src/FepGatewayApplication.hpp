#pragma once

#include "WorkerControllerAppBase.hpp"

#include <hyperapp/core/UpstreamGateway.hpp>
#include <hypernet/core/GlobalConfig.hpp>

#include <memory>

namespace fep
{

class FepGatewayApplication final : public apps::WorkerControllerAppBase
{
  public:
    explicit FepGatewayApplication(const hypernet::core::FepConfig &cfg);

    // IApplication 필수 구현
    void registerHandlers(hypernet::protocol::Dispatcher &dispatcher) override;
    void onServerStart() override;

    // [리팩토링] WorkerControllerAppBase 오버라이드
    // 부모 클래스(IApplication) 정의에 맞춰 noexcept 필수
    void setWorkerScheduler(std::shared_ptr<hypernet::IWorkerScheduler> scheduler) noexcept override;

    void onSessionEnd(hypernet::SessionHandle session) override;

  private:
    hypernet::core::FepConfig cfg_{};
    std::shared_ptr<hyperapp::UpstreamGateway> upstream_{};
};

} // namespace fep