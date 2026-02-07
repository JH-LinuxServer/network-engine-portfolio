#pragma once

#include <hypernet/Engine.hpp>
#include <memory>
#include <utility>

namespace hypernet::runtime
{

/// runtime 레이어의 최상위 실행 단위.
/// - 앱은 Server만 바라보고, Engine 내부는 추후 마음대로 교체/리팩토링 가능하게 만든다.
class Server final
{
  public:
    using Config = hypernet::EngineConfig;
    using AppPtr = std::shared_ptr<hypernet::IApplication>;

    Server(Config config, AppPtr app)
        : engine_(std::make_unique<hypernet::Engine>(config, std::move(app)))
    {
    }

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    Server(Server &&) noexcept = default;
    Server &operator=(Server &&) noexcept = default;

    void run() { engine_->run(); }
    void stop() noexcept { engine_->stop(); }

  private:
    std::unique_ptr<hypernet::Engine> engine_;
};

} // namespace hypernet::runtime