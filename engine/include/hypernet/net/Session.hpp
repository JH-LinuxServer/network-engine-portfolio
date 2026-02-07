#pragma once

#include <hypernet/SessionHandle.hpp>
#include <hypernet/net/FdHandler.hpp>
#include <hypernet/net/Socket.hpp>
#include <hypernet/util/NonCopyable.hpp>
#include <chrono>
#include <cstddef> // std::size_t
#include <cstdint>
#include <memory>

namespace hypernet::buffer
{
class RingBuffer; // forward declaration (구현은 Session.cpp에서 include)
}

namespace hypernet::net
{

class EventLoop;
class SessionManager;

/// 세션 상태머신(최소 고정)
enum class SessionState : std::uint8_t
{
    Connected = 0,
    Closing,
    Closed,
};

class Session final : private hypernet::util::NonCopyable,
                      public IFdHandler,
                      public std::enable_shared_from_this<Session>
{

  public:
    ~Session() override;

    [[nodiscard]] SessionHandle handle() const noexcept { return handle_; }
    [[nodiscard]] int ownerWorkerId() const noexcept { return ownerWorkerId_; }
    [[nodiscard]] SessionState state() const noexcept { return state_; }

    [[nodiscard]] int nativeHandle() const noexcept { return socket_.nativeHandle(); }
    [[nodiscard]] bool isOpen() const noexcept { return socket_.isValid(); }

    [[nodiscard]] static std::shared_ptr<Session>
    create(SessionHandle handle, int ownerWorkerId, Socket &&socket, SessionManager *ownerManager,
           std::size_t recvRingCapacity, std::size_t sendRingCapacity);

    // ===== IFdHandler =====
    [[nodiscard]] const char *fdTag() const noexcept override { return "session"; }
    [[nodiscard]] std::uint64_t fdDebugId() const noexcept override { return handle_.id(); }
    void handleEvent(EventLoop &loop, const EpollReactor::ReadyEvent &ev) override;
    bool enqueueSendNoFlush_(EventLoop &loop, const void *data, std::size_t len) noexcept;
    bool enqueuePacketU16Coalesced(EventLoop &loop, const std::uint8_t lenHdr4[4],
                                   const std::uint8_t opHdr2[2], const void *body,
                                   std::size_t bodyLen) noexcept;

  private:
    friend class SessionManager;

    struct PrivateTag
    {
        explicit PrivateTag() = default;
    };

  public:
    Session(PrivateTag, SessionHandle handle, int ownerWorkerId, Socket &&socket,
            SessionManager *ownerManager, std::size_t recvRingCapacity,
            std::size_t sendRingCapacity) noexcept;

  private:
    void onReadable_(EventLoop &loop) noexcept;
    bool processRecvFrames_(EventLoop &loop) noexcept;
    void onWritable_(EventLoop &loop) noexcept;
    void onError_(EventLoop &loop, const EpollReactor::ReadyEvent &ev) noexcept;

    void beginClose_(EventLoop &loop, const char *reason, int err) noexcept;

    /// 워커 종료 시 SessionManager가 강제로 정리할 때 사용합니다.
    /// - owner thread에서만 호출
    /// - manager 콜백(erase) 호출 금지
    void closeFromManager_(EventLoop &loop, const char *reason) noexcept;
    void closeFromManager_(EventLoop &loop, const char *reason, int err) noexcept;

    [[nodiscard]] bool flushSend_(EventLoop &loop) noexcept;

    void setWriteInterest_(EventLoop &loop, bool enable) noexcept;

    [[nodiscard]] static constexpr std::uint32_t baseEpollMask_() noexcept
    {
        return EpollReactor::makeEventMask({
            EpollReactor::Event::Read,
            EpollReactor::Event::EdgeTriggered,
            EpollReactor::Event::Error,
            EpollReactor::Event::Hangup,
            EpollReactor::Event::ReadHangup,
        });
    }

    void startTimeouts_(EventLoop &loop, std::uint32_t idleTimeoutMs,
                        std::uint32_t heartbeatIntervalMs) noexcept;

    void touchRx_() noexcept;

    void armIdleTimerAfter_(EventLoop &loop, std::chrono::milliseconds delay) noexcept;
    void onIdleTimer_(EventLoop &loop) noexcept;

    void armHeartbeatTimerAfter_(EventLoop &loop, std::chrono::milliseconds delay) noexcept;
    void onHeartbeatTimer_(EventLoop &loop) noexcept;

    std::uint32_t idleTimeoutMs_{0};
    std::uint32_t heartbeatIntervalMs_{0};

    std::chrono::steady_clock::time_point lastRxAt_{};
    bool idleTimerArmed_{false};
    bool heartbeatTimerArmed_{false};

    std::unique_ptr<hypernet::buffer::RingBuffer> recvRing_; // 생성 실패 시 close 정책 적용
    std::unique_ptr<hypernet::buffer::RingBuffer> sendRing_; // 생성 실패 시 close 정책 적용
    std::size_t recvRingCapacity_{0};
    std::size_t sendRingCapacity_{0};
    // 현재 epoll에 등록된 이벤트 마스크(디버깅/토글 중복 호출 방지용)
    std::uint32_t currentEpollMask_{baseEpollMask_()};

    SessionHandle handle_{};
    int ownerWorkerId_{-1};

    Socket socket_{};
    SessionState state_{SessionState::Connected};

    SessionManager *ownerManager_{nullptr};
};

} // namespace hypernet::net