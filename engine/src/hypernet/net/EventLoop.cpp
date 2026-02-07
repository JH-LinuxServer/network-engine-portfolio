
#include <hypernet/net/EventLoop.hpp>

#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <system_error>
#include <thread>

#include <unistd.h>
#include <sys/eventfd.h>

namespace hypernet::net
{

struct EventLoop::WakeupHandler final : IFdHandler
{
    explicit WakeupHandler(EventLoop *owner) noexcept : owner_(owner) {}

    [[nodiscard]] const char *fdTag() const noexcept override { return "eventfd"; }

    [[nodiscard]] std::uint64_t fdDebugId() const noexcept override
    {
        return owner_ ? static_cast<std::uint64_t>(owner_->wakeupFd_) : 0ULL;
    }

    void handleEvent(EventLoop &loop, const EpollReactor::ReadyEvent &ev) override
    {
        (void)loop;
        if (owner_)
        {
            owner_->handleWakeupEvent_(ev);
        }
    }

  private:
    EventLoop *owner_{nullptr};
};

namespace
{
[[noreturn]] void throwSysError(const char *what)
{
    throw std::system_error(errno, std::generic_category(), what);
}
} // namespace

EventLoop::EventLoop(Duration tickResolution, std::size_t timerSlots, int maxEpollEvents)
    : reactor_(maxEpollEvents), taskQueue_{}, timerWheel_(tickResolution, timerSlots)
{
    if (maxEpollEvents <= 0)
    {
        maxEpollEvents = 64;
    }
    readyEvents_.resize(static_cast<std::size_t>(maxEpollEvents));

    wakeupFd_ = ::eventfd(/*initval=*/0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0)
    {
        throwSysError("EventLoop: eventfd failed");
    }
    wakeupHandler_ = std::make_unique<WakeupHandler>(this);

    SLOG_INFO("EventLoop", "Created", "tick_ms={} timer_slots={} max_epoll_events={} wakeup_fd={}",
              timerWheel_.tickResolution().count(), timerWheel_.slotCount(), maxEpollEvents,
              wakeupFd_);
}

EventLoop::~EventLoop()
{
    if (wakeupFd_ >= 0)
    {
        ::close(wakeupFd_);
        SLOG_INFO("EventLoop", "WakeupFdClosed", "fd={}", wakeupFd_);
        wakeupFd_ = -1;
    }
}

void EventLoop::bindToCurrentThread() noexcept
{
    const auto thisThread = std::this_thread::get_id();

    if (ownerBound_.load(std::memory_order_acquire))
    {
        if (thisThread != ownerThread_)
        {
            SLOG_FATAL("EventLoop", "BindWrongThread",
                       "reason=AlreadyBound called_api=bindToCurrentThread");
            std::abort();
        }

        if (!wakeupRegistered_)
        {
            installWakeupFd_();
        }
        return;
    }

    ownerThread_ = thisThread;
    ownerBound_.store(true, std::memory_order_release);

    SLOG_INFO("EventLoop", "Bound", "api=bindToCurrentThread");

    // 초기화시에 만들었던 wakeupFd_{...}; 즉 event_fd 를  EventLoop 객체의
    //  EpollReactor 객체에 등록한다.
    installWakeupFd_();
}

bool EventLoop::isInOwnerThread() const noexcept
{
    if (!ownerBound_.load(std::memory_order_acquire))
    {
        return false;
    }
    return std::this_thread::get_id() == ownerThread_;
}

void EventLoop::assertInOwnerThread_(const char *apiName) const noexcept
{
    const int cw = core::wid();
    const long ct = core::tid();

    if (!ownerBound_.load(std::memory_order_acquire))
    {
        SLOG_FATAL("EventLoop", "ApiBeforeBind", "api='{}' wid={} tid={}",
                   apiName ? apiName : "(unknown)", cw, ct);
        std::abort();
    }

    if (std::this_thread::get_id() != ownerThread_)
    {
        SLOG_FATAL("EventLoop", "ApiWrongThread", "api='{}' wid={} tid={}",
                   apiName ? apiName : "(unknown)", cw, ct);
        std::abort();
    }
}

FdContext EventLoop::makeContext_(int fd, std::uint32_t events, IFdHandler *handler) noexcept
{
    FdContext ctx{};
    ctx.fd = fd;
    ctx.handler = handler;
    ctx.tag = handler ? handler->fdTag() : "unknown";
    ctx.debugId = handler ? handler->fdDebugId() : 0ULL;
    ctx.ownerPtr = reinterpret_cast<std::uintptr_t>(handler);
    ctx.registeredEvents = events;
    return ctx;
}

void EventLoop::installWakeupFd_() noexcept
{
    assertInOwnerThread_("installWakeupFd_");

    if (wakeupRegistered_)
    {
        return;
    }
    if (wakeupFd_ < 0 || !wakeupHandler_)
    {
        SLOG_FATAL("EventLoop", "WakeupInvalid", "fd={} handler_present={}", wakeupFd_,
                   (wakeupHandler_ ? 1 : 0));
        std::abort();
    }

    const auto mask = EpollReactor::makeEventMask({
        EpollReactor::Event::Read,
        EpollReactor::Event::EdgeTriggered,
        EpollReactor::Event::Error,
        EpollReactor::Event::Hangup,
        EpollReactor::Event::ReadHangup,
    });

    if (!addFd(wakeupFd_, mask, wakeupHandler_.get()))
    {
        SLOG_FATAL("EventLoop", "WakeupRegisterFailed", "fd={}", wakeupFd_);
        std::abort();
    }

    wakeupRegistered_ = true;
    SLOG_INFO("EventLoop", "WakeupRegistered", "fd={}", wakeupFd_);
}

void EventLoop::signalWakeup_() noexcept
{
    if (wakeupFd_ < 0)
    {
        return;
    }

    const std::uint64_t one = 1;

    for (;;)
    {
        const ::ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
        if (n == static_cast<::ssize_t>(sizeof(one)))
        {
            return;
        }

        if (n < 0 && errno == EINTR)
        {
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return;
        }

        SLOG_ERROR("EventLoop", "WakeupWriteFailed", "fd={} errno={} msg='{}'", wakeupFd_, errno,
                   std::strerror(errno));
        return;
    }
}

void EventLoop::handleWakeupEvent_(const EpollReactor::ReadyEvent &ev) noexcept
{
    if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
    {
        SLOG_ERROR("EventLoop", "WakeupFdError", "fd={} events=0x{:x}", ev.fd, ev.events);
    }

    if (ev.events & EPOLLIN)
    {
        drainWakeupFd_();
    }
}

void EventLoop::drainWakeupFd_() noexcept
{
    assertInOwnerThread_("drainWakeupFd_");

    if (wakeupFd_ < 0)
    {
        return;
    }

    for (;;)
    {
        std::uint64_t value = 0;
        const ::ssize_t n = ::read(wakeupFd_, &value, sizeof(value));
        if (n == static_cast<::ssize_t>(sizeof(value)))
        {
            continue;
        }

        if (n < 0 && errno == EINTR)
        {
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;
        }

        if (n == 0)
        {
            SLOG_WARN("EventLoop", "WakeupReadZero", "fd={}", wakeupFd_);
            break;
        }

        SLOG_ERROR("EventLoop", "WakeupReadFailed", "fd={} errno={} msg='{}'", wakeupFd_, errno,
                   std::strerror(errno));
        break;
    }
}

bool EventLoop::addFd(int fd, std::uint32_t events, IFdHandler *handler) noexcept
{
    assertInOwnerThread_("addFd");

    if (fd < 0)
    {
        errno = EBADF;
        SLOG_ERROR("EventLoop", "AddFdInvalidFd", "fd={}", fd);
        return false;
    }
    if (!handler)
    {
        errno = EINVAL;
        SLOG_ERROR("EventLoop", "AddFdNullHandler", "fd={}", fd);
        return false;
    }

    if (!reactor_.registerFd(fd, events))
    {
        SLOG_ERROR("EventLoop", "AddFdReactorRegisterFailed", "fd={} events=0x{:x}", fd, events);
        return false;
    }

    auto ctx = makeContext_(fd, events, handler);

    // fd context 단일화
    auto [it, inserted] = fdContexts_.emplace(fd, ctx);
    if (!inserted)
    {
        // 같은 fd가 이미 존재하는 것은 일반적으로 버그지만,
        // 운영 안전을 위해 overwrite 한다(로그로 남겨 추적).
        SLOG_WARN("EventLoop", "FdContextOverwrite", "fd={} tag={} id={}", fd, ctx.tag,
                  ctx.debugId);
        it->second = ctx;
    }

    SLOG_INFO("EventLoop", "FdRegistered", "fd={} tag={} id={} owner=0x{:x} events=0x{:x}", fd,
              ctx.tag, ctx.debugId, ctx.ownerPtr, ctx.registeredEvents);

    return true;
}

bool EventLoop::updateFd(int fd, std::uint32_t events) noexcept
{
    assertInOwnerThread_("updateFd");

    if (fd < 0)
    {
        errno = EBADF;
        SLOG_ERROR("EventLoop", "UpdateFdInvalidFd", "fd={}", fd);
        return false;
    }

    auto it = fdContexts_.find(fd);
    if (it == fdContexts_.end() || !it->second.handler)
    {
        errno = ENOENT;
        SLOG_ERROR("EventLoop", "UpdateFdMissingContext", "fd={}", fd);
        return false;
    }

    if (!reactor_.modifyFd(fd, events))
    {
        SLOG_ERROR("EventLoop", "UpdateFdReactorModifyFailed", "fd={} events=0x{:x}", fd, events);
        return false;
    }

    it->second.registeredEvents = events;
    SLOG_DEBUG("EventLoop", "UpdateFdOk", "fd={} events=0x{:x}", fd, events);
    return true;
}

bool EventLoop::removeFd(int fd) noexcept
{
    assertInOwnerThread_("removeFd");

    if (fd < 0)
    {
        errno = EBADF;
        SLOG_ERROR("EventLoop", "RemoveFdInvalidFd", "fd={}", fd);
        return false;
    }

    // 로그를 위해 컨텍스트를 복사해둔다(erase 이후에도 안전)
    FdContext ctxCopy{};
    if (auto it = fdContexts_.find(fd); it != fdContexts_.end())
    {
        ctxCopy = it->second;
    }

    const bool ok = reactor_.unregisterFd(fd);

    // registry 단일 소유: 여기서 제거
    (void)fdContexts_.erase(fd);

    if (!ok)
    {
        SLOG_WARN("EventLoop", "RemoveFdReactorUnregisterFalse", "fd={}", fd);
        return false;
    }

    if (ctxCopy.fd == fd && ctxCopy.handler)
    {
        SLOG_INFO("EventLoop", "FdUnregistered", "fd={} tag={} id={} owner=0x{:x}", fd, ctxCopy.tag,
                  ctxCopy.debugId, ctxCopy.ownerPtr);
    }
    else
    {
        SLOG_INFO("EventLoop", "FdUnregistered", "fd={} context=missing", fd);
    }

    return true;
}

void EventLoop::post(core::TaskQueue::Task task)
{
    taskQueue_.push(std::move(task));

    if (!isInOwnerThread())
    {
        signalWakeup_();
    }
}

core::TimerWheel::TimerId EventLoop::addTimer(Duration delay, core::TimerWheel::Callback cb)
{
    assertInOwnerThread_("addTimer");
    return timerWheel_.addTimer(delay, std::move(cb));
}

int EventLoop::computePollTimeoutMs() const noexcept
{
    const auto ms64 = timerWheel_.tickResolution().count();
    if (ms64 <= 0)
    {
        return 1;
    }
    if (ms64 > static_cast<long long>(std::numeric_limits<int>::max()))
    {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(ms64);
}

void EventLoop::drainTasks() noexcept
{
    core::TaskQueue::Task task;
    while (taskQueue_.tryPop(task))
    {
        if (!task)
        {
            continue;
        }
        try
        {
            task();
        }
        catch (const std::exception &e)
        {
            SLOG_ERROR("EventLoop", "TaskException", "what='{}'", e.what());
        }
        catch (...)
        {
            SLOG_ERROR("EventLoop", "TaskUnknownException");
        }
    }
}

void EventLoop::runOnce() noexcept
{
    drainTasks();
    timerWheel_.tick(core::TimerWheel::Clock::now());

    const int timeoutMs = computePollTimeoutMs();
    const int maxEvents = static_cast<int>(readyEvents_.size());

    int n = reactor_.wait(readyEvents_.data(), maxEvents, timeoutMs);
    if (n > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto &ev = readyEvents_[i];

            auto it = fdContexts_.find(ev.fd);
            if (it == fdContexts_.end() || !it->second.handler)
            {
                SLOG_WARN("EventLoop", "EventWithoutContext", "fd={} events=0x{:x}", ev.fd,
                          ev.events);
                continue;
            }

            const FdContext ctx = it->second;
            IFdHandler *handler = ctx.handler;

            SLOG_TRACE("EventLoop", "Dispatch",
                       "fd={} tag={} id={} owner=0x{:x} reg_events=0x{:x} ready_events=0x{:x}",
                       ev.fd, ctx.tag, ctx.debugId, ctx.ownerPtr, ctx.registeredEvents, ev.events);

            try
            {
                handler->handleEvent(*this, ev);
            }
            catch (const std::exception &e)
            {
                SLOG_ERROR("EventLoop", "HandlerException", "fd={} tag={} what='{}'", ev.fd,
                           ctx.tag, e.what());
            }
            catch (...)
            {
                SLOG_ERROR("EventLoop", "HandlerUnknownException", "fd={} tag={}", ev.fd, ctx.tag);
            }
        }
    }
    else if (n < 0)
    {
        if (errno != EINTR)
        {
            SLOG_WARN("EventLoop", "PollError", "errno={} msg='{}'", errno, std::strerror(errno));
        }
    }

    timerWheel_.tick(core::TimerWheel::Clock::now());
    drainTasks();
}

void EventLoop::run(std::atomic_bool &runningFlag) noexcept
{
    if (!ownerBound_.load(std::memory_order_acquire))
    {
        bindToCurrentThread();
    }
    else
    {
        assertInOwnerThread_("run");
    }

    while (runningFlag.load(std::memory_order_acquire))
    {
        runOnce();
    }
}

} // namespace hypernet::net
