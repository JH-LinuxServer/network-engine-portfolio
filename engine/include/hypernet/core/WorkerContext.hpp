#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <hypernet/core/Options.hpp>
#include <hypernet/buffer/BufferPool.hpp>
#include <hypernet/net/EventLoop.hpp>
#include <hypernet/util/NonCopyable.hpp>

namespace hypernet
{
class IApplication; // forward (앱 콜백 연결용)
} // namespace hypernet

namespace hypernet::net
{
class Acceptor;
class SessionManager;
} // namespace hypernet::net

namespace hypernet::core
{
class AppCallbackInvoker;

/// 하나의 논리적인 "워커 스레드"가 소유하는 엔진 내부 인프라 컨텍스트입니다.
///
/// ===== 스레딩/소유권 규칙(핵심) =====
/// - listen fd(리스닝 소켓)는 "리스너를 설치한 워커"가 단일 소유자.
/// - listen fd에 대한 epoll_ctl(add/mod/del) 및 close는 오직 소유 워커 스레드에서만 수행.
/// - accept된 client fd 또한 **소유 워커로 귀속**되며,
///   그 fd에 대한 epoll_ctl/read/write/close를 다른 스레드/다른 워커에서 수행하는 것은 금지.
/// - 다른 스레드가 워커에게 일을 시키려면 fd를 넘기지 말고 EventLoop::post로 "고수준 작업"만 전달.
/// - onSessionStart/onSessionEnd 는 "세션 owner 워커 스레드"에서 직접 호출되어야 한다.
/// - Engine main thread는 콜백을 호출하지 않고, 앱 포인터를 워커 컨텍스트로 주입만 한다.
class WorkerContext : private hypernet::util::NonCopyable
{
  public:
    using WorkerId = unsigned int;
    using Duration = TimerWheel::Duration;

    explicit WorkerContext(WorkerOptions options,
                           std::shared_ptr<hypernet::IApplication> app) noexcept;
    ~WorkerContext();

    WorkerContext(WorkerContext &&) = delete;
    WorkerContext &operator=(WorkerContext &&) = delete;

    void initialize();
    void shutdown();

    void configureListener(std::string listenAddress, std::uint16_t listenPort, int backlog = 128,
                           bool reusePort = true);

    void setAppCallbackInvoker(std::shared_ptr<AppCallbackInvoker> invoker) noexcept;

    [[nodiscard]] std::shared_ptr<AppCallbackInvoker> appCallbackInvoker() const noexcept
    {
        return appCallbacks_;
    }
    // Engine(main) thread에서 호출해도 안전 (실제 fd 작업은 owner worker에서 수행)
    void requestStopAccepting() noexcept;
    [[nodiscard]] std::size_t querySessionCountBlocking() noexcept;
    void requestCloseAllSessions(const char *reason) noexcept;

    void start();
    void stop() noexcept;
    void join() noexcept;

    void runOnce();

    [[nodiscard]] WorkerId id() const noexcept { return id_; }
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

    [[nodiscard]] bool isRunningFlagSet() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] TaskQueue &taskQueue() noexcept;
    [[nodiscard]] TimerWheel &timerWheel() noexcept;
    [[nodiscard]] hypernet::net::EventLoop &eventLoop() noexcept;
    [[nodiscard]] buffer::BufferPool &bufferPool() noexcept;

    // [추가됨] Dialer 구현을 위해 SessionManager 접근 허용
    hypernet::net::SessionManager &sessionManager() noexcept;
    const hypernet::net::SessionManager &sessionManager() const noexcept;

  private:
    WorkerId id_{0};
    WorkerOptions options_{};
    std::unique_ptr<hypernet::net::EventLoop> eventLoop_;
    std::unique_ptr<buffer::BufferPool> bufferPool_;

    // - 세션 수명/등록(IFdHandler*)을 per-worker로 고정
    std::unique_ptr<hypernet::net::SessionManager> sessionManager_;

    struct ListenerConfig
    {
        std::string address;
        std::uint16_t port{0};
        int backlog{128};
        bool reusePort{true};
    };

    bool listenerConfigured_{false};
    ListenerConfig listenerConfig_{};
    std::uint32_t idleTimeoutMs{0};
    std::uint32_t heartbeatIntervalMs{0};
    std::unique_ptr<hypernet::net::Acceptor> acceptor_;
    std::shared_ptr<AppCallbackInvoker> appCallbacks_;

    // - 실제 콜백 호출은 SessionManager(owner thread)에서만 수행한다.
    std::shared_ptr<hypernet::IApplication> app_;

    std::atomic_bool running_{false};
    std::thread thread_;
    bool initialized_{false};

    // 1.리스닝 소켓 생성 및 EpollReactor에 등록
    [[nodiscard]] bool installListenerInWorkerThread_() noexcept;
    void cleanupListenerInWorkerThread_() noexcept;
};

} // namespace hypernet::core
