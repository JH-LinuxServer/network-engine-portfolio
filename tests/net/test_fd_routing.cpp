#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/net/EventLoop.hpp>
#include <hypernet/net/FdHandler.hpp>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace
{

class TestEventfdHandler final : public hypernet::net::IFdHandler
{
  public:
    explicit TestEventfdHandler(int fd, bool *flag) : fd_(fd), flag_(flag) {}

    const char *fdTag() const noexcept override { return "eventfd_test"; }
    std::uint64_t fdDebugId() const noexcept override { return 1; }

    void handleEvent(hypernet::net::EventLoop &loop,
                     const hypernet::net::EpollReactor::ReadyEvent &ev) override
    {
        if ((ev.events & (EPOLLERR | EPOLLHUP)) != 0U)
        {
            // [수정] LOG_ERROR -> SLOG_ERROR
            SLOG_ERROR("TestEventfdHandler", "ErrorHup", "events=0x{:x}", ev.events);
            loop.removeFd(ev.fd);
            ::close(ev.fd);
            return;
        }

        if ((ev.events & EPOLLIN) == 0U)
        {
            return;
        }

        // ET 규약: EAGAIN까지 drain
        for (;;)
        {
            std::uint64_t v = 0;
            const ssize_t n = ::read(fd_, &v, sizeof(v));
            if (n == static_cast<ssize_t>(sizeof(v)))
            {
                if (flag_)
                {
                    *flag_ = true;
                }
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                break;
            }
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            break;
        }

        // 정리
        loop.removeFd(ev.fd);
        ::close(ev.fd);
    }

  private:
    int fd_{-1};
    bool *flag_{nullptr};
};

} // namespace

int main()
{
    using hypernet::net::EventLoop;

    // 단일 스레드 테스트이므로 worker id는 0처럼 설정해 로그 포맷을 맞춘다.
    hypernet::core::ThreadContext::setCurrentWorkerId(0);

    EventLoop loop(EventLoop::Duration{10}, /*timerSlots=*/64, /*maxEpollEvents=*/16);
    loop.bindToCurrentThread();

    const int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(efd >= 0);

    bool triggered = false;
    TestEventfdHandler handler(efd, &triggered);

    const std::uint32_t mask = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;

    const bool ok = loop.addFd(efd, mask, &handler);
    assert(ok);

    // 이벤트 발생
    const std::uint64_t one = 1;
    const ssize_t wn = ::write(efd, &one, sizeof(one));
    assert(wn == static_cast<ssize_t>(sizeof(one)));

    loop.runOnce();

    assert(triggered && "fd routing failed: handler was not triggered");
    return 0;
}