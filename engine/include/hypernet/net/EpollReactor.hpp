#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>

#include <hypernet/util/NonCopyable.hpp>

namespace hypernet::net
{

class EpollReactor : private hypernet::util::NonCopyable
{
  public:
    using Fd = int;

    /// epoll_event.events 에서 사용할 플래그 집합입니다.
    ///
    /// - C 매크로(EPOLLIN, EPOLLOUT, ...)를 enum class 로 감싸 이름을 드러냅니다.
    /// - 실제 epoll_ctl 호출 시에는 makeEventMask() 로 std::uint32_t 마스크로 변환합니다.
    enum class Event : std::uint32_t
    {
        None = 0,
        Read = EPOLLIN,
        Write = EPOLLOUT,
        ReadHangup = EPOLLRDHUP,
        Priority = EPOLLPRI,
        Error = EPOLLERR,
        Hangup = EPOLLHUP,
        EdgeTriggered = EPOLLET,
        OneShot = EPOLLONESHOT,
    };

    /// epoll_wait 결과를 엔진 코드에 전달하기 위한 구조체입니다.
    struct ReadyEvent
    {
        Fd fd{-1};
        std::uint32_t events{0}; ///< EPOLLIN | EPOLLOUT | EPOLLET ... 의 비트 OR
        void *userData{nullptr}; ///< registerFd/modifyFd 에서 등록한 포인터 (nullable)
    };

    /// @param maxEvents 한 번의 wait() 에서 처리할 최대 이벤트 개수 힌트입니다.
    ///        내부적으로 이 크기의 epoll_event 버퍼를 미리 할당해 재사용합니다.
    explicit EpollReactor(int maxEvents = 64);

    ~EpollReactor() noexcept;

    // 복사는 NonCopyable 로 금지, 이동도 의도적으로 금지합니다.
    // (두 인스턴스가 같은 epoll fd 를 중복 close 하는 실수를 방지)
    EpollReactor(EpollReactor &&) = delete;
    EpollReactor &operator=(EpollReactor &&) = delete;

    /// 여러 Event 플래그를 OR 하여 epoll_event.events 에 넣을 정수 마스크를 생성합니다.
    ///
    /// 예:
    ///   auto mask = EpollReactor::makeEventMask(
    ///       {Event::Read, Event::EdgeTriggered});
    static constexpr std::uint32_t makeEventMask(std::initializer_list<Event> events) noexcept
    {
        std::uint32_t mask = 0;
        for (auto e : events)
        {
            mask |= static_cast<std::uint32_t>(e);
        }
        return mask;
    }

    bool registerFd(Fd fd, std::uint32_t events) noexcept;
    bool modifyFd(Fd fd, std::uint32_t events) noexcept;

    /// fd 를 epoll 인스턴스에서 제거합니다. (EPOLL_CTL_DEL)
    ///
    /// - 아직 등록되지 않은 fd 에 대해 호출하면 false 를 반환하고 경고 로그를 남깁니다.
    bool unregisterFd(Fd fd) noexcept;

    /// 준비된 이벤트를 기다립니다. (epoll_wait thin wrapper)
    ///
    /// @param outEvents 준비된 이벤트를 써 넣을 버퍼입니다.
    /// @param maxEvents outEvents 에 담을 수 있는 최대 이벤트 개수입니다.
    /// @param timeoutMs 타임아웃(ms). -1 이면 무한 대기, 0 이면 즉시 리턴(폴링).
    ///
    /// @return
    ///   - >= 0 : 준비된 이벤트 개수 (0 이면 타임아웃)
    ///   - -1   : 오류 (errno 확인). EINTR 인 경우 DEBUG 로그만 남기고 -1 반환.
    int wait(ReadyEvent *outEvents, int maxEvents, int timeoutMs) noexcept;

    /// std::span 버전의 wait 헬퍼입니다.
    int wait(std::span<ReadyEvent> events, int timeoutMs) noexcept
    {
        if (events.empty())
        {
            return 0;
        }
        return wait(events.data(), static_cast<int>(events.size()), timeoutMs);
    }

    /// 내부 epoll fd 를 그대로 노출합니다. (테스트/디버깅 용도)
    [[nodiscard]] Fd nativeHandle() const noexcept { return epollFd_; }

  private:
    Fd epollFd_{-1};
    int maxEvents_{0};
    std::vector<::epoll_event> eventBuffer_; ///< epoll_wait 용 임시 버퍼 (재사용)
};

/// Event 비트 OR 연산자.
///
/// - enum class 는 기본적으로 비트 연산이 금지되어 있으므로, 의도적으로 허용합니다.
/// - 사용 예:
///   auto mask = EpollReactor::makeEventMask({Event::Read, Event::EdgeTriggered});
constexpr EpollReactor::Event operator|(EpollReactor::Event lhs, EpollReactor::Event rhs) noexcept
{
    return static_cast<EpollReactor::Event>(static_cast<std::uint32_t>(lhs) |
                                            static_cast<std::uint32_t>(rhs));
}

} // namespace hypernet::net
