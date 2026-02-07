#pragma once

#include <hypernet/core/TaskQueue.hpp>
#include <hypernet/core/TimerWheel.hpp>
#include <hypernet/net/EpollReactor.hpp>
#include <hypernet/net/FdContext.hpp>
#include <hypernet/net/FdHandler.hpp>
#include <hypernet/util/NonCopyable.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hypernet::net
{

class EventLoop : private hypernet::util::NonCopyable
{
  public:
    using Duration = core::TimerWheel::Duration;

    EventLoop(Duration tickResolution, std::size_t timerSlots, int maxEpollEvents = 64);
    ~EventLoop();

    // 1.새로 생성되는 워커스레드가 해당 이벤트 루프 소유자로 등록됨
    //-ownerThread_ = thisThread;
    // 2.메인스레드가WorkerContext를 생성하고 초기화했던
    //-wakeupFd_ 를  해당 WorkerContext 의 EpollReactor 에 등록
    void bindToCurrentThread() noexcept;
    [[nodiscard]] bool isInOwnerThread() const noexcept;

    bool addFd(int fd, std::uint32_t events, IFdHandler *handler) noexcept;
    bool updateFd(int fd, std::uint32_t events) noexcept;
    bool removeFd(int fd) noexcept;

    void post(core::TaskQueue::Task task);

    core::TimerWheel::TimerId addTimer(Duration delay, core::TimerWheel::Callback cb);

    void runOnce() noexcept;
    void run(std::atomic_bool &runningFlag) noexcept;

    [[nodiscard]] core::TaskQueue &taskQueue() noexcept { return taskQueue_; }
    [[nodiscard]] core::TimerWheel &timerWheel() noexcept { return timerWheel_; }
    [[nodiscard]] EpollReactor &reactor() noexcept { return reactor_; }

  private:
    EpollReactor reactor_;
    core::TaskQueue taskQueue_;
    core::TimerWheel timerWheel_;

    std::vector<EpollReactor::ReadyEvent> readyEvents_;

    // fd → {handler, tag, debugId, ownerPtr, events} 단일 레지스트리
    std::unordered_map<int, FdContext> fdContexts_;

    std::atomic_bool ownerBound_{false};
    std::thread::id ownerThread_{};

    void assertInOwnerThread_(const char *apiName) const noexcept;

    void drainTasks() noexcept;
    [[nodiscard]] int computePollTimeoutMs() const noexcept;

    // ===== wakeup(eventfd) =====
    struct WakeupHandler;
    int wakeupFd_{-1};
    std::unique_ptr<WakeupHandler> wakeupHandler_;
    bool wakeupRegistered_{false};

    void installWakeupFd_() noexcept;
    void signalWakeup_() noexcept;
    void handleWakeupEvent_(const EpollReactor::ReadyEvent &ev) noexcept;
    void drainWakeupFd_() noexcept;

    [[nodiscard]] FdContext makeContext_(int fd, std::uint32_t events,
                                         IFdHandler *handler) noexcept;
};

} // namespace hypernet::net
