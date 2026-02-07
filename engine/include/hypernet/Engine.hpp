#pragma once

#include <hypernet/EngineConfig.hpp>
#include <hypernet/IApplication.hpp>
#include <hypernet/util/NonCopyable.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// POSIX sigwait/pthread_kill
#include <pthread.h>
#include <signal.h>

namespace hypernet::core
{
struct EngineOptions;
class WorkerContext;
class AppCallbackInvoker;
} // namespace hypernet::core

namespace hypernet::monitoring
{
class HttpStatusServer;
}

namespace hypernet
{

class Engine : private util::NonCopyable
{
  public:
    enum class EngineState : std::uint8_t
    {
        Stopped,
        Running,
        Stopping
    };

    enum class StopSource : std::uint8_t
    {
        None,
        Api,
        Signal
    };

    Engine(const EngineConfig &config, std::shared_ptr<IApplication> app);
    ~Engine();

    void run();
    void stop() noexcept;

    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

  private:
    using Workers = std::vector<std::unique_ptr<core::WorkerContext>>;

    void resetForRun_() noexcept;
    void startMetrics_();
    void stopMetrics_() noexcept;

    [[nodiscard]] Workers createWorkers_(const core::EngineOptions &opt);
    [[nodiscard]] std::shared_ptr<core::AppCallbackInvoker> setupAppCallbacks_(Workers &workers);
    void setupRouting_(Workers &workers);
    void startWorkers_(Workers &workers);

    void logStarted_(const core::EngineOptions &opt) const;

    // [REFAC] 메인 스레드가 sigwait()로 "완전 대기" (추가 스레드 없음)
    void initSignalWait_() noexcept; // MUST run before creating any other threads
    void waitForStop_() noexcept;    // blocks until SIGINT/SIGTERM or stop()

    void shutdownGracefully_(Workers &workers, const core::EngineOptions &opt, const std::shared_ptr<core::AppCallbackInvoker> &appInvoker) noexcept;

    void requestStop_(StopSource src, int signo) noexcept;

    EngineConfig config_;
    std::shared_ptr<IApplication> app_;
    std::unique_ptr<monitoring::HttpStatusServer> metricsServer_;

    std::atomic_bool running_{false};
    std::atomic<EngineState> state_{EngineState::Stopped};
    std::atomic<StopSource> stopSource_{StopSource::None};
    std::atomic_int stopSignal_{0};

    // main thread id (for waking sigwait on stop())
    pthread_t mainTid_{};
    std::atomic_bool mainTidReady_{false};

    // sigwait mask (signals are blocked in all threads; main thread consumes them via sigwait)
    sigset_t waitSigset_{};
};

} // namespace hypernet
