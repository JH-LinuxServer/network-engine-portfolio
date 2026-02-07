#pragma once

#include <hypernet/util/NonCopyable.hpp>

#include <atomic>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace hypernet::core
{

enum class LogLevel : int
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

namespace detail
{
// 로그 필터링 최적화를 위한 전역 atomic 변수 (Logger.cpp에서 정의)
std::atomic<int> &fastMinLevel();
} // namespace detail

// Hot path용 빠른 레벨 체크
inline bool fastEnabled(LogLevel level) noexcept
{
    return static_cast<int>(level) >= detail::fastMinLevel().load(std::memory_order_relaxed);
}

// 로깅 인터페이스 (레거시: file/line 제거)
class ILogger : private hypernet::util::NonCopyable
{
  public:
    virtual ~ILogger() = default;

    [[nodiscard]] virtual LogLevel minLevel() const noexcept { return LogLevel::Trace; }
    virtual void shutdown() noexcept {}

    // [NEW] 메시지는 이미 "comp | evt | key=value..." 형태로 만들어서 넣는다.
    virtual void log(LogLevel level, std::string_view message) = 0;
};

// 기본 Logger 구현체 (Console/Stream 기반, 비동기)
class Logger final : public ILogger
{
  public:
    explicit Logger(std::ostream &os = std::clog);
    ~Logger() override;

    void log(LogLevel level, std::string_view message) override;

    void setMinLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel minLevel() const noexcept override;

    // 명시적 중단 (스레드 조인 및 잔여 로그 플러시)
    void stopAndJoin();
    void shutdown() noexcept override { stopAndJoin(); }

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Global instance
ILogger &getLogger();
void setLogger(std::shared_ptr<ILogger> logger) noexcept;
void shutdownLogger() noexcept;

// =============================================================================
// Structured Logging Frontend
//   최종 라인: "HH:MM:SS.uuuuuu | W0 tid=123 | INFO  | comp | evt | k=v ..."
//   - prefix(time/thread/level)는 Logger가 찍는다
//   - message(payload)는 "comp | evt | key=value"만 남긴다
// =============================================================================
namespace slog
{
inline std::string build(std::string_view comp, std::string_view evt, std::string_view details)
{
    if (details.empty())
        return std::format("{} | {}", comp, evt);
    return std::format("{} | {} | {}", comp, evt, details);
}

inline void emit(LogLevel lvl, std::string_view comp, std::string_view evt)
{
    if (!fastEnabled(lvl))
        return;
    getLogger().log(lvl, build(comp, evt, {}));
}

template <typename... Args>
inline void emit(LogLevel lvl, std::string_view comp, std::string_view evt,
                 std::format_string<Args...> fmt, Args &&...args)
{
    if (!fastEnabled(lvl))
        return;
    std::string details = std::format(fmt, std::forward<Args>(args)...);
    getLogger().log(lvl, build(comp, evt, details));
}
} // namespace slog

// C++20: __VA_OPT__로 fmt 유무 둘 다 지원
#define SLOG_TRACE(comp, evt, ...)                                                                 \
    ::hypernet::core::slog::emit(::hypernet::core::LogLevel::Trace, (comp),                        \
                                 (evt)__VA_OPT__(, ) __VA_ARGS__)
#define SLOG_DEBUG(comp, evt, ...)                                                                 \
    ::hypernet::core::slog::emit(::hypernet::core::LogLevel::Debug, (comp),                        \
                                 (evt)__VA_OPT__(, ) __VA_ARGS__)
#define SLOG_INFO(comp, evt, ...)                                                                  \
    ::hypernet::core::slog::emit(::hypernet::core::LogLevel::Info, (comp),                         \
                                 (evt)__VA_OPT__(, ) __VA_ARGS__)
#define SLOG_WARN(comp, evt, ...)                                                                  \
    ::hypernet::core::slog::emit(::hypernet::core::LogLevel::Warn, (comp),                         \
                                 (evt)__VA_OPT__(, ) __VA_ARGS__)
#define SLOG_ERROR(comp, evt, ...)                                                                 \
    ::hypernet::core::slog::emit(::hypernet::core::LogLevel::Error, (comp),                        \
                                 (evt)__VA_OPT__(, ) __VA_ARGS__)
#define SLOG_FATAL(comp, evt, ...)                                                                 \
    ::hypernet::core::slog::emit(::hypernet::core::LogLevel::Fatal, (comp),                        \
                                 (evt)__VA_OPT__(, ) __VA_ARGS__)

// =============================================================================
// Legacy Kill Switch
//   - 기존 LOG_* / HNET_LOG_* 는 완전 금지(컴파일 에러로 강제)
// =============================================================================
#define HYPERNET_LEGACY_LOG_REMOVED()                                                              \
    do                                                                                             \
    {                                                                                              \
        static_assert(false, "Legacy LOG_* removed. Use SLOG_*(comp, evt, \"k=v ...\") only.");    \
    } while (0)

#define LOG_TRACE(...) HYPERNET_LEGACY_LOG_REMOVED()
#define LOG_DEBUG(...) HYPERNET_LEGACY_LOG_REMOVED()
#define LOG_INFO(...) HYPERNET_LEGACY_LOG_REMOVED()
#define LOG_WARN(...) HYPERNET_LEGACY_LOG_REMOVED()
#define LOG_ERROR(...) HYPERNET_LEGACY_LOG_REMOVED()
#define LOG_FATAL(...) HYPERNET_LEGACY_LOG_REMOVED()

#define HNET_LOG_TRACE(...) HYPERNET_LEGACY_LOG_REMOVED()
#define HNET_LOG_DEBUG(...) HYPERNET_LEGACY_LOG_REMOVED()
#define HNET_LOG_INFO(...) HYPERNET_LEGACY_LOG_REMOVED()
#define HNET_LOG_WARN(...) HYPERNET_LEGACY_LOG_REMOVED()
#define HNET_LOG_ERROR(...) HYPERNET_LEGACY_LOG_REMOVED()
#define HNET_LOG_FATAL(...) HYPERNET_LEGACY_LOG_REMOVED()

} // namespace hypernet::core
