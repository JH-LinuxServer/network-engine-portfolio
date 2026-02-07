#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <sys/types.h>  // ssize_t
#include <sys/socket.h> // sockaddr, socklen_t

#include <hypernet/util/NonCopyable.hpp>

namespace hypernet::net {

/// POSIX 소켓 파일 디스크립터를 RAII 로 감싸는 얇은 래퍼입니다.
///
/// - 생성/소멸 시점에 ::socket / ::close 를 직접 호출하지 않고 이 클래스를 통해 관리합니다.
/// - move-only 객체로, 소유권은 항상 정확히 한 Socket 인스턴스에만 존재합니다.
/// - epoll 등 다른 OS API 와 연동할 때는 nativeHandle() 으로 원시 fd 를 가져다 쓸 수 있습니다.
/// - 이 타입은 "연결 상태"나 프로토콜 개념을 알지 못하는, 순수한 OS 레벨 래퍼입니다.
class Socket : private hypernet::util::NonCopyable {
  public:
    using Handle = int;

    /// 비어 있는(유효하지 않은) 소켓을 생성합니다. nativeHandle() == -1 입니다.
    Socket() noexcept = default;

    /// 이미 생성된 fd 의 소유권을 넘겨받습니다.
    explicit Socket(Handle fd) noexcept;

    /// 소멸 시 fd 가 유효하다면 ::close 를 호출합니다.
    ~Socket() noexcept;

    /// move 생성자: 다른 Socket 으로부터 fd 소유권을 훔쳐옵니다.
    Socket(Socket &&other) noexcept;

    /// move 대입 연산자: 기존 fd 를 닫고, 다른 Socket 의 fd 소유권을 넘겨받습니다.
    Socket &operator=(Socket &&other) noexcept;

    /// 현재 소켓이 유효한 fd 를 가지고 있는지 여부입니다.
    [[nodiscard]] bool isValid() const noexcept { return fd_ >= 0; }

    /// epoll / poll / getsockname 등에 사용하기 위한 원시 fd 입니다.
    [[nodiscard]] Handle nativeHandle() const noexcept { return fd_; }

    /// TCP/IPv4 스트림 소켓을 생성합니다.
    [[nodiscard]] static Socket createTcpIPv4() noexcept;

    /// TCP/IPv6 스트림 소켓을 생성합니다.
    [[nodiscard]] static Socket createTcpIPv6() noexcept;

    /// 소켓을 닫습니다. 이후 isValid() == false 가 됩니다.
    ///
    /// - 이미 닫힌 소켓에 대해 호출해도 안전합니다.(idempotent)
    void close() noexcept;

    /// 소켓을 논블로킹 또는 블로킹 모드로 전환합니다.
    [[nodiscard]] bool setNonBlocking(bool enable) noexcept;

    /// SO_REUSEADDR 옵션을 설정합니다.
    [[nodiscard]] bool setReuseAddr(bool enable) noexcept;

    /// SO_REUSEPORT 옵션을 설정합니다. (플랫폼에서 지원하지 않으면 false 반환)
    [[nodiscard]] bool setReusePort(bool enable) noexcept;

    /// TCP_NODELAY 옵션을 설정합니다. (Nagle 알고리즘 on/off)
    [[nodiscard]] bool setNoDelay(bool enable) noexcept;

    /// 지정된 주소로 bind 합니다.
    [[nodiscard]] bool bind(const ::sockaddr *addr, ::socklen_t len) noexcept;

    /// IPv4 문자열 주소/포트로 bind 합니다. (예: "0.0.0.0", 9000)
    [[nodiscard]] bool bind(const std::string &ip, std::uint16_t port) noexcept;

    /// listen backlog 를 설정하고 수동 대기 상태로 전환합니다.
    [[nodiscard]] bool listen(int backlog) noexcept;

    /// 새 연결을 accept 합니다. 실패 시 isValid()==false 인 Socket 을 반환합니다.
    [[nodiscard]] Socket accept(::sockaddr *addr, ::socklen_t *len) noexcept;

    /// 원격 주소가 필요 없을 때 사용하는 편의 accept 오버로드입니다.
    [[nodiscard]] Socket accept() noexcept;

    /// 지정된 주소로 connect 합니다.
    [[nodiscard]] bool connect(const ::sockaddr *addr, ::socklen_t len) noexcept;

    /// IPv4 문자열 주소/포트로 connect 합니다.
    [[nodiscard]] bool connect(const std::string &ip, std::uint16_t port) noexcept;

    /// send(2) thin 래퍼입니다.
    ///
    /// - 반환값은 send(2) 와 동일 의미를 가집니다.
    /// - 실패 시 -1, errno 에 구체적인 오류 코드가 설정됩니다.
    [[nodiscard]] ::ssize_t send(const void *data, std::size_t len, int flags = 0) noexcept;

    /// recv(2) thin 래퍼입니다.
    ///
    /// - 반환값은 recv(2) 와 동일 의미를 가집니다.
    /// - 실패 시 -1, errno 에 구체적인 오류 코드가 설정됩니다.
    [[nodiscard]] ::ssize_t recv(void *buffer, std::size_t len, int flags = 0) noexcept;

  private:
    Handle fd_{-1};
};

} // namespace hypernet::net
