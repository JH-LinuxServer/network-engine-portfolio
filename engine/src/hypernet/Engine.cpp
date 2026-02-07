#include <hypernet/Engine.hpp>

#include <hypernet/core/AppCallbacks.hpp>
#include <hypernet/core/EffectiveOptions.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/LoggingConfig.hpp>
#include <hypernet/core/WorkerContext.hpp>

#include <hypernet/monitoring/HttpStatusServer.hpp>
#include <hypernet/monitoring/Metrics.hpp>

#include <hypernet/net/EventLoop.hpp>
#include <hypernet/net/SessionRouterFactory.hpp>
#include <hypernet/net/WorkerSchedulerFactory.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace hypernet
{

// 내부 wake-up 시그널(메인스레드 sigwait 깨우기 용)
// (프로젝트에서 SIGUSR1을 다른 용도로 쓰면 SIGRTMIN+N 같은 걸로 바꾸는 게 안전합니다.)
static constexpr int kWakeSignal = SIGUSR1;

Engine::Engine(const EngineConfig &config, std::shared_ptr<IApplication> app) : config_(config), app_(std::move(app))
{
    validateEngineConfig(config_);
    core::applyLoggingConfig(config_);
}

Engine::~Engine() = default;

// ============================================================
//  [NEW] sigwait 기반: 모든 스레드에서 SIGINT/SIGTERM 등을 block
//  그리고 메인 스레드가 sigwait()로 받아서 종료 트리거
// ============================================================
void Engine::initSignalWait_() noexcept
{
    sigemptyset(&waitSigset_);

    sigaddset(&waitSigset_, SIGINT);  // Ctrl+C
    sigaddset(&waitSigset_, SIGTERM); // kill -TERM
    sigaddset(&waitSigset_, SIGQUIT); // (선택) 필요 없으면 제거 가능

    // 내부 wake-up( stop()이 메인 sigwait를 깨움 )
    sigaddset(&waitSigset_, kWakeSignal);

    // MUST: 워커/메트릭 등 다른 스레드 만들기 전에 호출해야 "상속"됨
    (void)pthread_sigmask(SIG_BLOCK, &waitSigset_, nullptr);
}

// ============================================================
//  [NEW] 메인스레드: polling 없이 sigwait로 완전 block
// ============================================================
void Engine::waitForStop_() noexcept
{
    // stop()이 이미 호출돼서 running_이 false라면 절대 대기하지 않음
    if (!running_.load(std::memory_order_acquire))
        return;

    for (;;)
    {
        int signo = 0;
        const int rc = sigwait(&waitSigset_, &signo);
        if (rc != 0)
            continue;

        // stop()이 깨운 경우(또는 teardown용 wake)
        if (signo == kWakeSignal)
        {
            if (!running_.load(std::memory_order_acquire))
                break; // 종료 요청 확정 -> 아래 shutdown으로 진행
            continue;  // running_이 아직 true면(희박) 다시 대기
        }

        // 외부 시그널(SIGINT/SIGTERM/...)이면 stop 요청
        requestStop_(StopSource::Signal, signo);
        break;
    }
}

void Engine::run()
{
    const core::EngineOptions opt = core::makeEffectiveEngineOptions(config_);

    Workers workers;
    std::shared_ptr<core::AppCallbackInvoker> appInvoker;

    // main thread id 기록 (stop()에서 깨우기 위해)
    mainTid_ = pthread_self();
    mainTidReady_.store(true, std::memory_order_release);

    try
    {
        // [핵심] 반드시 다른 스레드 생성 전에!
        initSignalWait_();

        resetForRun_();
        startMetrics_();

        workers = createWorkers_(opt);

        appInvoker = setupAppCallbacks_(workers);
        setupRouting_(workers);

        startWorkers_(workers);

        SLOG_INFO("HyperNet", "ThreadAffinity", "Disabled (OS Scheduling Mode)");

        if (appInvoker)
        {
            appInvoker->postAndWait("onServerStart", [](IApplication &app) { app.onServerStart(); });
        }

        logStarted_(opt);

        // [REFAC] 메인 스레드는 여기서 "완전히 잠듦"
        // - Ctrl+C(SIGINT) / SIGTERM 오면 sigwait가 깨움 -> requestStop_ -> shutdown
        // - stop() 오면 SIGUSR1로 깨움 -> running_ false 확인 -> shutdown
        waitForStop_();

        shutdownGracefully_(workers, opt, appInvoker);
        stopMetrics_();

        state_.store(EngineState::Stopped, std::memory_order_release);
        SLOG_INFO("HyperNet", "EngineStopped", "");
    }
    catch (const std::exception &e)
    {
        SLOG_FATAL("HyperNet", "EngineRunAborted", "reason=Exception what='{}'", e.what());
        running_.store(false, std::memory_order_release);

        shutdownGracefully_(workers, opt, appInvoker);
        stopMetrics_();
        state_.store(EngineState::Stopped, std::memory_order_release);

        mainTidReady_.store(false, std::memory_order_release);
        throw;
    }
    catch (...)
    {
        SLOG_FATAL("HyperNet", "EngineRunAborted", "reason=UnknownException");
        running_.store(false, std::memory_order_release);

        shutdownGracefully_(workers, opt, appInvoker);
        stopMetrics_();
        state_.store(EngineState::Stopped, std::memory_order_release);

        mainTidReady_.store(false, std::memory_order_release);
        throw;
    }

    mainTidReady_.store(false, std::memory_order_release);
}

void Engine::resetForRun_() noexcept
{
    stopSource_.store(StopSource::None, std::memory_order_release);
    stopSignal_.store(0, std::memory_order_release);
    state_.store(EngineState::Running, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    hypernet::monitoring::engineMetrics().reset();
}

void Engine::startMetrics_()
{
    if (config_.metricsHttpPort == 0)
        return;

    metricsServer_ = std::make_unique<hypernet::monitoring::HttpStatusServer>(config_.metricsHttpAddress, config_.metricsHttpPort);

    if (!metricsServer_->start())
    {
        metricsServer_.reset();
        throw std::runtime_error("[MetricsHTTP] failed to start metrics server");
    }
}

void Engine::stopMetrics_() noexcept
{
    if (!metricsServer_)
        return;
    metricsServer_->stopAndJoin();
    metricsServer_.reset();
}

Engine::Workers Engine::createWorkers_(const core::EngineOptions &opt)
{
    Workers workers;
    workers.reserve(opt.workerCount);

    for (unsigned int i = 0; i < opt.workerCount; ++i)
    {
        auto wopt = core::makeWorkerOptions(opt, i);
        wopt.idleTimeoutMs = config_.idleTimeoutMs;
        wopt.heartbeatIntervalMs = config_.heartbeatIntervalMs;

        auto w = std::make_unique<core::WorkerContext>(std::move(wopt), app_);

        w->initialize();
        if (config_.listenPort != 0)
        {
            w->configureListener(config_.listenAddress, config_.listenPort, opt.listenBacklog, config_.reusePort);
        }
        workers.push_back(std::move(w));
    }
    return workers;
}

std::shared_ptr<core::AppCallbackInvoker> Engine::setupAppCallbacks_(Workers &workers)
{
    if (!app_ || workers.empty())
        return {};

    auto invoker = std::make_shared<core::AppCallbackInvoker>(app_, &workers.front()->eventLoop(), 0);
    for (auto &w : workers)
        w->setAppCallbackInvoker(invoker);
    return invoker;
}

void Engine::setupRouting_(Workers &workers)
{
    std::vector<hypernet::net::EventLoop *> loops;
    loops.reserve(workers.size());
    for (auto &w : workers)
        loops.push_back(&w->eventLoop());

    auto router = hypernet::net::makeGlobalSessionRouter(loops);
    auto scheduler = hypernet::net::makeGlobalWorkerScheduler(std::move(loops));

    if (app_)
    {
        app_->setSessionRouter(router);
        app_->setWorkerScheduler(scheduler);
    }
}

void Engine::startWorkers_(Workers &workers)
{
    try
    {
        for (auto &w : workers)
            w->start();
    }
    catch (const std::exception &e)
    {
        SLOG_FATAL("HyperNet", "StartWorkersFailed", "reason=Exception what='{}'", e.what());
        for (auto &w : workers)
        {
            w->stop();
            w->shutdown();
        }
        running_.store(false, std::memory_order_release);
        state_.store(EngineState::Stopped, std::memory_order_release);
        throw;
    }
}

void Engine::logStarted_(const core::EngineOptions &opt) const
{
    SLOG_INFO("HyperNet", "EngineStarted", "listen_addr='{}' listen_port={} workers={} reuse_port={} metrics_bind='{}:{}'", config_.listenAddress, config_.listenPort, opt.workerCount,
              (config_.reusePort ? "on" : "off"), config_.metricsHttpAddress, config_.metricsHttpPort);

    SLOG_INFO("HyperNet", "WorkerRuntime",
              "drain_ms={} poll_ms={} tick_ms={} timer_slots={} max_epoll_events={} "
              "buffer_block_size={} buffer_blocks={} recv_ring_bytes={} send_ring_bytes={} "
              "max_payload_len={}",
              opt.shutdownDrainTimeout.count(), opt.shutdownPollInterval.count(), opt.workerDefaults.timer.tickResolution.count(), opt.workerDefaults.timer.slotCount,
              opt.workerDefaults.eventLoop.maxEpollEvents, opt.workerDefaults.bufferPool.blockSize, opt.workerDefaults.bufferPool.blockCount, opt.workerDefaults.rings.recvCapacity,
              opt.workerDefaults.rings.sendCapacity, opt.workerDefaults.protocol.maxPayloadLen);
}

void Engine::shutdownGracefully_(Workers &workers, const core::EngineOptions &opt, const std::shared_ptr<core::AppCallbackInvoker> &appInvoker) noexcept
{
    if (workers.empty())
        return;

    SLOG_INFO("HyperNet", "ShutdownPhase1StopAccepting", "");
    for (auto &w : workers)
        w->requestStopAccepting();

    SLOG_INFO("HyperNet", "ShutdownPhase2DrainSessions", "timeout_ms={}", (int)opt.shutdownDrainTimeout.count());
    const auto deadline = std::chrono::steady_clock::now() + opt.shutdownDrainTimeout;
    std::size_t remaining = 0;

    for (;;)
    {
        remaining = 0;
        for (auto &w : workers)
            remaining += w->querySessionCountBlocking();
        if (remaining == 0)
            break;
        if (std::chrono::steady_clock::now() >= deadline)
            break;
        std::this_thread::sleep_for(opt.shutdownPollInterval);
    }

    if (remaining != 0)
    {
        SLOG_WARN("HyperNet", "ShutdownDrainTimeoutForceClose", "remaining={}", remaining);
        for (auto &w : workers)
            w->requestCloseAllSessions("engine_shutdown_timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    SLOG_INFO("HyperNet", "ShutdownPhase3StopWorkers", "scope=ExceptWorker0");
    for (std::size_t i = 1; i < workers.size(); ++i)
    {
        try
        {
            workers[i]->shutdown();
        }
        catch (...)
        {
            SLOG_ERROR("HyperNet", "WorkerShutdownFailed", "wid={}", i);
        }
    }

    if (appInvoker)
    {
        SLOG_INFO("HyperNet", "ShutdownPhase4OnServerStop", "target=Worker0");
        try
        {
            appInvoker->postAndWait("onServerStop", [](IApplication &app) { app.onServerStop(); });
        }
        catch (...)
        {
        }
    }

    SLOG_INFO("HyperNet", "ShutdownPhase5StopWorker0", "");
    try
    {
        workers[0]->shutdown();
    }
    catch (...)
    {
    }
}

void Engine::stop() noexcept
{
    requestStop_(StopSource::Api, 0);

    // 메인 스레드가 sigwait로 자고 있으면 깨움
    if (mainTidReady_.load(std::memory_order_acquire))
    {
        (void)pthread_kill(mainTid_, kWakeSignal);
    }
}

void Engine::requestStop_(StopSource src, int signo) noexcept
{
    StopSource expected = StopSource::None;
    if (!stopSource_.compare_exchange_strong(expected, src))
        return;

    stopSignal_.store(signo, std::memory_order_release);
    state_.store(EngineState::Stopping, std::memory_order_release);

    SLOG_INFO("HyperNet", "StopRequested", "src={} signo={}", (src == StopSource::Signal ? "signal" : "api"), signo);

    running_.store(false, std::memory_order_release);
    if (metricsServer_)
        metricsServer_->requestStop();
}

} // namespace hypernet
