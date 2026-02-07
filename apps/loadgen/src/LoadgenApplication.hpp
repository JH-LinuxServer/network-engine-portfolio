#pragma once

#include "WorkerControllerAppBase.hpp" // 경로가 apps/common/WorkerControllerAppBase.hpp여야 함

#include <hypernet/core/GlobalConfig.hpp>

namespace client
{

class LoadgenApplication final : public fep::apps::WorkerControllerAppBase
{
  public:
    explicit LoadgenApplication(const hypernet::core::ExchangeSimConfig &cfg); // noexcept 제거 (vector 할당 가능성)

    void registerHandlers(hypernet::protocol::Dispatcher &dispatcher) override;

    void onServerStart() override;

  private:
    hypernet::core::ExchangeSimConfig cfg_{};
};

} // namespace client