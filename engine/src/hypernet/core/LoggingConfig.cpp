#include <hypernet/core/LoggingConfig.hpp>
#include <hypernet/core/Logger.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace hypernet::core
{
namespace
{

// std::ostream 수명을 소유하면서, 내부적으로 기존 Logger를 사용해 비동기 로그를 유지한다.
class OwningOstreamLogger final : public ILogger
{
  public:
    explicit OwningOstreamLogger(std::shared_ptr<std::ostream> os, LogLevel lvl) noexcept
        : os_(std::move(os)), logger_(*os_)
    {
        logger_.setMinLevel(lvl);
    }

    void log(LogLevel level, std::string_view message) override
    {
        // 내부 로거 호출도 인자 2개로 변경
        logger_.log(level, message);
    }
    [[nodiscard]] LogLevel minLevel() const noexcept override { return logger_.minLevel(); }
    void shutdown() noexcept override { logger_.stopAndJoin(); }

  private:
    std::shared_ptr<std::ostream> os_;
    Logger logger_;
};

} // namespace

void applyLoggingConfig(const hypernet::EngineConfig &cfg)
{
    if (cfg.logFilePath.empty())
    {
        auto os = std::shared_ptr<std::ostream>(&std::clog, [](std::ostream *) {});
        setLogger(std::make_shared<OwningOstreamLogger>(std::move(os), cfg.logLevel));
        return;
    }

    auto file = std::make_shared<std::ofstream>(cfg.logFilePath, std::ios::app);
    if (!file->is_open())
    {
        throw std::runtime_error("[LoggingConfig] failed to open log file: " + cfg.logFilePath);
    }

    std::shared_ptr<std::ostream> os = file;
    setLogger(std::make_shared<OwningOstreamLogger>(std::move(os), cfg.logLevel));
}

} // namespace hypernet::core
