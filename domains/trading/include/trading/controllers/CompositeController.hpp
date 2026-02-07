#pragma once

#include <trading/controllers/IController.hpp>

#include <memory>
#include <vector>

namespace trading::controllers
{
class CompositeController final : public trading::IController
{
  public:
    CompositeController() = default;

    void add(std::shared_ptr<trading::IController> child);

    void install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime) override;

    void onServerStart() override;
    void onServerStop() override;

    void onSessionStart(hypernet::SessionHandle session) override;
    void onSessionEnd(hypernet::SessionHandle session) override;

  private:
    std::vector<std::shared_ptr<trading::IController>> children_;
};
} // namespace trading::controllers
