#pragma once

#include <cstdint>

#include <hypernet/net/EpollReactor.hpp>

namespace hypernet::net {

class EventLoop;

/// fd 이벤트 라우팅 규약을 강제하기 위한 공통 핸들러 인터페이스입니다.
///
/// ===== 라우팅 규칙(고정) =====
/// - EventLoop에 등록되는 모든 fd는 "단일 IFdHandler 객체"를 userData로 가진다.
/// - epoll에서 이벤트가 오면 ReadyEvent.userData(IFdHandler*)를 통해
///   정확히 그 객체의 handleEvent()로만 전달한다.
///
/// ===== 스레딩/소유권 규약(중요) =====
/// - handleEvent()는 해당 EventLoop의 owner thread(=워커 스레드)에서만 호출된다.
/// - IFdHandler 객체의 수명은 "fd가 EventLoop에 등록되어 있는 동안" 반드시 유효해야 한다.
/// - cross-thread fd 접근 금지 규약을 깨지 않도록,
///   handleEvent() 내부에서만 fd I/O/close/epoll_ctl을 수행하는 패턴을 권장한다.
///
/// ===== 디버깅 규약 =====
/// - fdTag(): "acceptor", "session", "eventfd" 같은 owner type 태그
/// - fdDebugId(): session id 등 추적용 숫자(없으면 0)
class IFdHandler {
  public:
    virtual ~IFdHandler() = default;

    /// 디버깅용 owner type 태그(고정 문자열 권장)
    [[nodiscard]] virtual const char *fdTag() const noexcept = 0;

    /// 디버깅용 식별자(예: SessionHandle::id). 없으면 0.
    [[nodiscard]] virtual std::uint64_t fdDebugId() const noexcept = 0;

    /// epoll 이벤트를 처리합니다.
    /// - EventLoop owner thread에서만 호출됩니다.
    virtual void handleEvent(EventLoop &loop, const EpollReactor::ReadyEvent &ev) = 0;
};

} // namespace hypernet::net
