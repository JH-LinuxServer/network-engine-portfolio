#include <hypernet/core/WorkerContext.hpp>

#include <hypernet/core/AppCallbacks.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/net/Acceptor.hpp>
#include <hypernet/net/EpollReactor.hpp>
#include <hypernet/net/SessionManager.hpp>
#include <hypernet/net/WorkerLocal.hpp>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <future>
#include <thread>

#include <sys/syscall.h>
#include <unistd.h>

namespace hypernet::core
{

WorkerContext::WorkerContext(WorkerOptions options, std::shared_ptr<hypernet::IApplication> app) noexcept : id_(static_cast<WorkerId>(options.id)), options_(std::move(options)), app_(std::move(app))
{
}
WorkerContext::~WorkerContext()
{
    stop();
    if (initialized_)
    {
        try
        {
            shutdown();
        }
        catch (...)
        {
        }
    }
}
void WorkerContext::initialize()
{
    if (initialized_)
    {
        SLOG_WARN("WorkerContext", "InitIgnored", "reason=AlreadyInitialized");
        return;
    }
    if (!app_)
    {
        SLOG_ERROR("WorkerContext", "InitFailed", "reason=NullApplication");
        return;
    }
    eventLoop_ = std::make_unique<net::EventLoop>(options_.timer.tickResolution, options_.timer.slotCount, options_.eventLoop.maxEpollEvents);

    bufferPool_ = std::make_unique<buffer::BufferPool>(options_.bufferPool.blockSize, options_.bufferPool.blockCount);

    // per-worker SessionManager 생성 (+ rings/framer 정책 전달)
    sessionManager_ = std::make_unique<net::SessionManager>(id_, eventLoop_.get(), options_.rings.recvCapacity, options_.rings.sendCapacity, options_.protocol.maxPayloadLen);

    initialized_ = true;

    SLOG_INFO("WorkerContext", "Initialized",
              "tick_ms={} slots={} epoll_max={} block_size={} block_cnt={} recv_cap={} send_cap={} "
              "max_payload={}",
              options_.timer.tickResolution.count(), options_.timer.slotCount, options_.eventLoop.maxEpollEvents, options_.bufferPool.blockSize, options_.bufferPool.blockCount,
              options_.rings.recvCapacity, options_.rings.sendCapacity, options_.protocol.maxPayloadLen);
}
void WorkerContext::configureListener(std::string listenAddress, std::uint16_t listenPort, int backlog, bool reusePort)
{
    if (running_.load(std::memory_order_acquire) || thread_.joinable())
    {
        SLOG_ERROR("WorkerContext", "ConfigListenerIgnored", "reason=AlreadyRunning");
        return;
    }

    listenerConfig_.address = std::move(listenAddress);
    listenerConfig_.port = listenPort;
    listenerConfig_.backlog = backlog;
    listenerConfig_.reusePort = reusePort;
    listenerConfigured_ = true;

    SLOG_INFO("WorkerContext", "ListenerConfigured", "addr={} port={} backlog={} reuse_port={}", listenerConfig_.address, listenerConfig_.port, listenerConfig_.backlog,
              listenerConfig_.reusePort ? "on" : "off");
}

void WorkerContext::setAppCallbackInvoker(std::shared_ptr<AppCallbackInvoker> invoker) noexcept
{
    appCallbacks_ = std::move(invoker);
}

bool WorkerContext::installListenerInWorkerThread_() noexcept
{
    // const long tid = hypernet::core::ThreadContext::currentTid(); // Logger handles TID

    if (listenerConfig_.port == 0)
    {
        SLOG_INFO("WorkerContext", "ListenerDisabled", "reason=PortZero");
        return true;
    }

    try
    {
        acceptor_ = std::make_unique<net::Acceptor>(listenerConfig_.address, listenerConfig_.port, listenerConfig_.backlog, listenerConfig_.reusePort);
    }
    catch (const std::exception &e)
    {
        SLOG_FATAL("WorkerContext", "AcceptorCreateFailed", "reason='{}'", e.what());
        acceptor_.reset();
        return false;
    }

    if (!acceptor_->setNonBlocking(true))
    {
        SLOG_WARN("WorkerContext", "SetNonBlockingFailed", "errno={} msg='{}'", errno, std::strerror(errno));
    }

    acceptor_->setAcceptCallback(
        [this](net::Socket &&client, const net::Acceptor::PeerEndpoint &peer)
        {
            if (!sessionManager_)
            {
                SLOG_FATAL("WorkerContext", "OnAcceptBug", "reason=SessionManagerNull");
                std::abort();
            }
            auto h = sessionManager_->onAccepted(std::move(client), peer);
            (void)h;
        });

    const auto acceptMask = net::EpollReactor::makeEventMask({
        net::EpollReactor::Event::Read,
        net::EpollReactor::Event::EdgeTriggered,
        net::EpollReactor::Event::Error,
        net::EpollReactor::Event::Hangup,
        net::EpollReactor::Event::ReadHangup,
    });

    const int listenFd = acceptor_->nativeHandle();

    const bool ok = eventLoop_->addFd(listenFd, acceptMask, acceptor_.get());
    if (!ok)
    {
        SLOG_FATAL("WorkerContext", "RegisterListenFdFailed", "fd={}", listenFd);
        acceptor_->close();
        acceptor_.reset();
        return false;
    }

    SLOG_INFO("WorkerContext", "ListenerInstalled", "addr={} port={} fd={} reuse_port={}", listenerConfig_.address, listenerConfig_.port, listenFd, listenerConfig_.reusePort ? "on" : "off");

    return true;
}

void WorkerContext::requestStopAccepting() noexcept
{
    if (!eventLoop_)
        return;

    // owner thread면 바로 실행(데드락 방지)
    if (eventLoop_->isInOwnerThread())
    {
        cleanupListenerInWorkerThread_(); // removeFd + close + reset
                                          // :contentReference[oaicite:7]{index=7}
        return;
    }

    eventLoop_->post([this] { cleanupListenerInWorkerThread_(); });
}

std::size_t WorkerContext::querySessionCountBlocking() noexcept
{
    if (!eventLoop_ || !sessionManager_)
        return 0;

    if (eventLoop_->isInOwnerThread())
    {
        return sessionManager_->sessionCount();
    }

    auto p = std::make_shared<std::promise<std::size_t>>();
    auto f = p->get_future();

    eventLoop_->post(
        [this, p]
        {
            if (!sessionManager_)
            {
                p->set_value(0);
                return;
            }
            p->set_value(sessionManager_->sessionCount());
        });

    // shutdown 중이므로 “무한 대기”는 피하는 게 좋음(안전장치)
    if (f.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready)
    {
        return 0;
    }
    return f.get();
}

void WorkerContext::requestCloseAllSessions(const char *reason) noexcept
{
    if (!eventLoop_ || !sessionManager_)
        return;

    if (eventLoop_->isInOwnerThread())
    {
        sessionManager_->closeAllByPolicy(reason, 0);
        return;
    }

    // reason은 string literal 권장
    eventLoop_->post(
        [this, reason]
        {
            if (sessionManager_)
            {
                sessionManager_->closeAllByPolicy(reason, 0);
            }
        });
}

void WorkerContext::cleanupListenerInWorkerThread_() noexcept
{
    if (!acceptor_ || !eventLoop_)
    {
        return;
    }

    // const long tid = ThreadContext::currentTid();
    const int fd = acceptor_->nativeHandle();

    if (fd >= 0)
    {
        (void)eventLoop_->removeFd(fd);
    }

    acceptor_->close();
    acceptor_.reset();

    SLOG_INFO("WorkerContext", "ListenerCleanedUp", "fd={}", fd);
}
void WorkerContext::start()
{

    if (!initialized_)
    {
        SLOG_ERROR("WorkerContext", "StartFailed", "reason=NotInitialized");
        return;
    }
    if (!eventLoop_ || !sessionManager_ || !bufferPool_)
    {
        SLOG_ERROR("WorkerContext", "StartFailed", "reason=PrerequisitesMissing");
        return;
    }
    if (!app_)
    {
        SLOG_ERROR("WorkerContext", "StartFailed", "reason=AppMissing");
        return;
    }

    const bool alreadyRunning = running_.exchange(true, std::memory_order_acq_rel);
    if (alreadyRunning)
    {
        SLOG_WARN("WorkerContext", "StartIgnored", "reason=AlreadyRunning");
        return;
    }

    // 메인스레드가 워커스레드를 생성하기 전에 joinable 상태 즉 스레드가 join 가능한 상태라면
    // 결함이므로 에러 처리
    if (thread_.joinable())
    {
        SLOG_ERROR("WorkerContext", "StartFailed", "reason=ThreadJoinable action=ForcingJoin");
        join();
    }

    // readyPromise 를 생성해서 이후 생성될 워커스레드에게 넘긴다
    // 워커스레드가 이 readyPromise 에 신호를 보내면 메인스레드는
    // readyFuture 로 대기하다가 깨어난다.
    std::promise<bool> readyPromise;
    auto readyFuture = readyPromise.get_future();

    try
    {
        thread_ = std::thread(
            [this, p = std::move(readyPromise)]() mutable
            {
                ThreadContext::setCurrentWorkerId(static_cast<int>(id_));

                if (sessionManager_ && app_)
                {
                    sessionManager_->setApplication(app_);
                }
                sessionManager_->configureTimeouts(options_.idleTimeoutMs, options_.heartbeatIntervalMs);
                SLOG_INFO("WorkerContext", "TimeoutsConfigured", "idle_ms={} heartbeat_ms={}", options_.idleTimeoutMs, options_.heartbeatIntervalMs);
                SLOG_INFO("WorkerContext", "ThreadStarted", "");

                if (eventLoop_)
                {
                    eventLoop_->bindToCurrentThread();
                    hypernet::net::WorkerLocal::set(sessionManager_.get());
                }

                const bool installed = installListenerInWorkerThread_();

                try
                {
                    p.set_value(installed);
                }
                catch (...)
                {
                }

                if (!installed)
                {
                    running_.store(false, std::memory_order_release);
                    cleanupListenerInWorkerThread_();
                    SLOG_FATAL("WorkerContext", "ListenerInstallFailed", "action=WorkerExiting");
                    return;
                }

                eventLoop_->run(running_);

                if (sessionManager_)
                {
                    sessionManager_->shutdownInOwnerThread();
                }

                cleanupListenerInWorkerThread_();
                SLOG_INFO("WorkerContext", "ThreadExiting", "");
                hypernet::net::WorkerLocal::set(static_cast<hypernet::net::SessionManager *>(nullptr));
                hypernet::net::WorkerLocal::set(static_cast<hypernet::net::ConnectorManager *>(nullptr));
            });
    }
    catch (...)
    {
        running_.store(false, std::memory_order_release);
        throw;
    }

    bool ok = false;
    try
    {
        ok = readyFuture.get();
    }
    catch (...)
    {
        ok = false;
    }

    if (!ok)
    {
        SLOG_FATAL("WorkerContext", "ListenerInstallFailed", "action=StoppingWorker");
        stop();
        throw std::runtime_error("WorkerContext: listener install failed");
    }
}

void WorkerContext::join() noexcept
{
    if (!thread_.joinable())
    {
        return;
    }

    if (thread_.get_id() == std::this_thread::get_id())
    {
        SLOG_FATAL("WorkerContext", "JoinSelfDetected", "action=Detaching");
        thread_.detach();
        return;
    }

    thread_.join();
}

void WorkerContext::stop() noexcept
{
    running_.store(false, std::memory_order_release);
    join();
}

void WorkerContext::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    stop();

    SLOG_INFO("WorkerContext", "ShuttingDown", "");

    // sessionManager_의 내부 세션들은 start()의 worker thread 종료 경로에서
    // shutdownInOwnerThread()로 이미 정리되어 있어야 한다.
    sessionManager_.reset();

    // acceptor_는 정상 경로에서는 워커 스레드에서 이미 cleanup 되었어야 한다.
    acceptor_.reset();

    eventLoop_.reset();
    bufferPool_.reset();

    initialized_ = false;

    SLOG_INFO("WorkerContext", "ShutdownComplete", "");
}

void WorkerContext::runOnce()
{
    if (!initialized_)
    {
        SLOG_WARN("WorkerContext", "RunOnceIgnored", "reason=NotInitialized");
        return;
    }

    // 단일 스레드 테스트/디버깅에서 runOnce가 사용될 수 있으므로,
    // 이 호출 스레드를 EventLoop owner로 바인딩한다.
    eventLoop_->bindToCurrentThread();
    eventLoop_->runOnce();
}

TaskQueue &WorkerContext::taskQueue() noexcept
{
    assert(initialized_ && "WorkerContext must be initialized before accessing taskQueue()");
    return eventLoop_->taskQueue();
}

TimerWheel &WorkerContext::timerWheel() noexcept
{
    assert(initialized_ && "WorkerContext must be initialized before accessing timerWheel()");
    return eventLoop_->timerWheel();
}

net::EventLoop &WorkerContext::eventLoop() noexcept
{
    assert(initialized_ && "WorkerContext must be initialized before accessing eventLoop()");
    return *eventLoop_;
}

buffer::BufferPool &WorkerContext::bufferPool() noexcept
{
    assert(initialized_ && "WorkerContext must be initialized before accessing bufferPool()");
    return *bufferPool_;
}

hypernet::net::SessionManager &WorkerContext::sessionManager() noexcept
{
    return *sessionManager_;
}

const hypernet::net::SessionManager &WorkerContext::sessionManager() const noexcept
{
    return *sessionManager_;
}

} // namespace hypernet::core