#pragma once

#include <cstdint> // uint64_t, uintptr_t

namespace hypernet::net {

class IFdHandler;

/// epoll에 등록된 fd에 대한 "디버깅/라우팅 컨텍스트"입니다.
///
///
/// - fd → {handler, type tag, debug id, owner ptr, events} 를 한 곳(EventLoop)에 모아
///   디버깅 시 "이 fd가 무엇인지"를 즉시 추적 가능하게 만든다.
///
/// 수명/스레딩 규약(중요):
/// - FdContext는 EventLoop(owner thread)에서만 생성/수정/삭제한다.
/// - handler 포인터는 non-owning 이며, "fd가 EventLoop에 등록되어 있는 동안" 유효해야 한다.
/// - 디스패치 시점에는 fd로 컨텍스트를 다시 조회하여, unregister 이후의 지연 이벤트(close race)를
///   안전하게 무시할 수 있도록 한다(UAF 방지).
struct FdContext {
    int fd{-1};
    IFdHandler *handler{nullptr};      ///< non-owning (등록 동안 유효해야 함)
    const char *tag{"unknown"};        ///< handler->fdTag() 결과(고정 문자열 권장)
    std::uint64_t debugId{0};          ///< handler->fdDebugId() (예: session id)
    std::uintptr_t ownerPtr{0};        ///< 디버깅용 포인터(보통 handler 주소)
    std::uint32_t registeredEvents{0}; ///< epoll_ctl에 등록된 events mask
};

} // namespace hypernet::net
