#pragma once

#include <hypernet/EngineConfig.hpp>
#include <hypernet/IApplication.hpp>
#include <hypernet/runtime/Server.hpp>

#include <memory>
#include <stdexcept>
#include <utility>

namespace hypernet::runtime
{

/// Server 생성 진입점(프레임워크 표면).
/// - 여기서 "공개 API 표면"을 고정해두면, 엔진 내부는 이후에 분리/교체가 쉬워진다.
class ServerBuilder final
{
  public:
    using Config = hypernet::EngineConfig;
    using AppPtr = std::shared_ptr<hypernet::IApplication>;

    ServerBuilder() = default;

    ServerBuilder &config(Config cfg)
    {
        config_ = std::move(cfg);
        return *this;
    }

    ServerBuilder &application(AppPtr app)
    {
        app_ = std::move(app);
        return *this;
    }

    template <typename App, typename... Args> ServerBuilder &makeApplication(Args &&...args)
    {
        app_ = std::make_shared<App>(std::forward<Args>(args)...);
        return *this;
    }

    [[nodiscard]] Server build()
    {
        if (!app_)
            throw std::invalid_argument("hypernet::runtime::ServerBuilder: application is null");
        hypernet::validateEngineConfig(config_);
        return Server{config_, std::move(app_)};
    }

  private:
    Config config_{};
    AppPtr app_{};
};

} // namespace hypernet::runtime