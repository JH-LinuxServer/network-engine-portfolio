#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <hypernet/net/FdHandler.hpp>
#include <hypernet/net/Socket.hpp>
#include <hypernet/util/NonCopyable.hpp>

namespace hypernet::net {

/// TCP 리스닝 소켓을 소유하고, epoll 이벤트(accept fd)를 처리하는 클래스입니다.
///
/// ===== fd 라우팅 규약(고정) =====
/// - accept fd는 EventLoop에 "Acceptor(this)"를 handler(userData)로 등록한다.
/// - 이벤트는 Acceptor::handleEvent()로만 들어오며,
///   내부에서 onReadable()/onError()로 분기한다.
///
/// ===== 스레딩/소유권 규약(중요) =====
/// - listen fd의 epoll_ctl/close/accept는 "리스너를 설치한 워커 스레드"에서만 수행한다.
/// - accept된 client fd 역시 accept한 워커에 귀속되며, 다른 워커로 이동시키지 않는다.
///   (SessionManager가 붙을 때도 동일 워커 내에서만 소유권을 넘긴다.)
class Acceptor final : private hypernet::util::NonCopyable, public IFdHandler {
  public:
    struct PeerEndpoint {
        std::string ip;
        std::uint16_t port{0};
    };

    using AcceptCallback = std::function<void(Socket &&client, const PeerEndpoint &peer)>;

    Acceptor(std::string listenAddress, std::uint16_t listenPort, int backlog = 128,
             bool reusePort = true);

    ~Acceptor() = default;

    /// acceptOne() 호출 시 새 연결을 하나 수락합니다.
    [[nodiscard]] Socket acceptOne(PeerEndpoint *outPeer = nullptr) noexcept;

    void close() noexcept { listenSocket_.close(); }
    [[nodiscard]] bool isValid() const noexcept { return listenSocket_.isValid(); }

    [[nodiscard]] std::string_view listenAddress() const noexcept { return listenAddress_; }
    [[nodiscard]] std::uint16_t listenPort() const noexcept { return listenPort_; }
    [[nodiscard]] int nativeHandle() const noexcept { return listenSocket_.nativeHandle(); }

    [[nodiscard]] bool setNonBlocking(bool enable) noexcept {
        return listenSocket_.setNonBlocking(enable);
    }

    /// accept 성공 시 호출될 콜백을 설정합니다.
    ///
    /// - 콜백은 accept를 수행한 워커 스레드에서 동기 호출됩니다.
    /// - 콜백은 client 소켓을 "같은 워커 스레드 내부에서만" 소비해야 합니다.
    void setAcceptCallback(AcceptCallback cb) noexcept { onAccept_ = std::move(cb); }

    // ===== IFdHandler =====
    [[nodiscard]] const char *fdTag() const noexcept override { return "acceptor"; }
    [[nodiscard]] std::uint64_t fdDebugId() const noexcept override {
        return static_cast<std::uint64_t>(nativeHandle());
    }
    void handleEvent(EventLoop &loop, const EpollReactor::ReadyEvent &ev) override;

  private:
    Socket listenSocket_;
    std::string listenAddress_;
    std::uint16_t listenPort_{0};
    int backlog_{128};

    AcceptCallback onAccept_;

    void refreshBoundPort() noexcept;
    static void fillPeerEndpoint(const ::sockaddr *sa, ::socklen_t salen,
                                 PeerEndpoint &out) noexcept;

    void onReadable_();
    void onError_(EventLoop &loop, const EpollReactor::ReadyEvent &ev) noexcept;
};

} // namespace hypernet::net
