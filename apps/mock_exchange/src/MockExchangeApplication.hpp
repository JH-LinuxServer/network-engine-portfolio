#pragma once

#include "WorkerControllerAppBase.hpp" // apps/common/WorkerControllerAppBase.hpp

#include <hypernet/protocol/Dispatcher.hpp>

namespace exchange
{

class MockExchangeApplication final : public fep::apps::WorkerControllerAppBase
{
  public:
    // [수정] vector 할당 가능성이 있으므로 noexcept 제거
    MockExchangeApplication();

    void registerHandlers(hypernet::protocol::Dispatcher &dispatcher) noexcept override;

    void onServerStart() override;
};

} // namespace exchange