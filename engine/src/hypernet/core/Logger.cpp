#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp> // ttag(), tid()

#include <chrono>
#include <condition_variable>
#include <cstring> // strrchr (unused now, kept optional)
#include <format>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <unistd.h> // isatty, fileno
#include <vector>

namespace hypernet::core
{

namespace detail
{
std::atomic<int> &fastMinLevel()
{
    static std::atomic<int> level{static_cast<int>(LogLevel::Info)};
    return level;
}
} // namespace detail

static const char *levelToStr(LogLevel lvl) noexcept
{
    switch (lvl)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    }
    return "INFO";
}

// 로그 항목 구조체 (레거시 file/line 완전 제거)
struct LogEvent
{
    LogLevel level{};
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::string threadTag; // e.g. "main", "w0"
    long threadId{};
};

class Logger::Impl
{
  public:
    explicit Impl(std::ostream &os) : os_(os)
    {
        // 터미널인지 확인하여 색상 사용 여부 결정 (stdout 기준)
        if (&os == &std::cout || &os == &std::clog || &os == &std::cerr)
        {
            useColor_ = (::isatty(::fileno(stdout)) != 0);
        }
        worker_ = std::thread([this]() { processQueue(); });
    }

    ~Impl() { stop(); }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void log(LogLevel level, std::string_view msg)
    {
        // capture time + thread meta at log-call time
        auto now = std::chrono::system_clock::now();
        std::string ttag(hypernet::core::ttag());
        long tid = hypernet::core::tid();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(LogEvent{level, std::string(msg), now, std::move(ttag), tid});
        }
        cv_.notify_one();
    }

    void setMinLevel(LogLevel level) noexcept
    {
        minLevel_ = level;
        // fast path filter도 함께 갱신
        detail::fastMinLevel().store(static_cast<int>(level), std::memory_order_relaxed);
    }

    LogLevel minLevel() const noexcept { return minLevel_; }

  private:
    void processQueue()
    {
        while (true)
        {
            std::vector<LogEvent> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });

                if (stop_ && queue_.empty())
                    return;

                while (!queue_.empty())
                {
                    batch.push_back(std::move(queue_.front()));
                    queue_.pop();
                }
            }

            for (const auto &ev : batch)
            {
                if (ev.level < minLevel_)
                    continue;
                writeLog(ev);
            }
            os_.flush();
        }
    }

    void writeLog(const LogEvent &ev)
    {
        using namespace std::chrono;

        // time-of-day (HH:MM:SS.uuuuuu)
        const auto t = system_clock::to_time_t(ev.timestamp);
        std::tm tm{};
        localtime_r(&t, &tm);

        const auto us = duration_cast<microseconds>(ev.timestamp.time_since_epoch()) % seconds(1);

        // thread column: "W0 tid=..." / "main tid=..."
        std::string thr = ev.threadTag;
        if (!thr.empty() && thr[0] == 'w')
            thr[0] = 'W';
        const std::string thrCol = std::format("{} tid={}", thr, ev.threadId);

        // level (fixed width)
        const char *lvl = levelToStr(ev.level);
        const std::string lvlCol = std::format("{:<5}", lvl);

        // color only around level if tty
        const char *c1 = "";
        const char *c2 = "";
        if (useColor_)
        {
            switch (ev.level)
            {
            case LogLevel::Trace:
                c1 = "\x1b[90m";
                c2 = "\x1b[0m";
                break; // gray
            case LogLevel::Debug:
                c1 = "\x1b[36m";
                c2 = "\x1b[0m";
                break; // cyan
            case LogLevel::Info:
                c1 = "\x1b[32m";
                c2 = "\x1b[0m";
                break; // green
            case LogLevel::Warn:
                c1 = "\x1b[33m";
                c2 = "\x1b[0m";
                break; // yellow
            case LogLevel::Error:
            case LogLevel::Fatal:
                c1 = "\x1b[31m";
                c2 = "\x1b[0m";
                break; // red
            }
        }

        // FINAL: time | thread | level | message
        os_ << std::format("{:02d}:{:02d}:{:02d}.{:06d} | {} | {}{}{} | {}\n", tm.tm_hour,
                           tm.tm_min, tm.tm_sec, (int)us.count(), thrCol, c1, lvlCol, c2,
                           ev.message);
    }

    std::ostream &os_;
    std::thread worker_;
    std::queue<LogEvent> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_{false};
    LogLevel minLevel_{LogLevel::Info};
    bool useColor_{false};
};

Logger::Logger(std::ostream &os) : impl_(std::make_unique<Impl>(os)) {}
Logger::~Logger() = default;

void Logger::log(LogLevel level, std::string_view message)
{
    impl_->log(level, message);
}
void Logger::setMinLevel(LogLevel level) noexcept
{
    impl_->setMinLevel(level);
}
LogLevel Logger::minLevel() const noexcept
{
    return impl_->minLevel();
}

void Logger::stopAndJoin()
{
    impl_->stop();
}

// ===== Global Instance Management =====

static std::shared_ptr<ILogger> &globalLoggerStorage()
{
    static std::shared_ptr<ILogger> logger = std::make_shared<Logger>();
    return logger;
}

ILogger &getLogger()
{
    auto &instance = globalLoggerStorage();
    if (!instance)
    {
        instance = std::make_shared<Logger>();
    }
    return *instance;
}

void setLogger(std::shared_ptr<ILogger> logger) noexcept
{
    if (logger)
    {
        detail::fastMinLevel().store(static_cast<int>(logger->minLevel()),
                                     std::memory_order_relaxed);
    }
    else
    {
        detail::fastMinLevel().store(static_cast<int>(LogLevel::Info), std::memory_order_relaxed);
    }
    globalLoggerStorage() = std::move(logger);
}

void shutdownLogger() noexcept
{
    auto &instance = globalLoggerStorage();
    if (!instance)
        return;

    // 종료 시점(Cold path)에서 안전하게 join까지 수행
    if (auto *impl = dynamic_cast<Logger *>(instance.get()))
    {
        impl->stopAndJoin();
    }
    else
    {
        instance->shutdown();
    }
    instance.reset();
}

} // namespace hypernet::core
