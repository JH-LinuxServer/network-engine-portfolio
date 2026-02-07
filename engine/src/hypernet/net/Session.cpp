
#include <hypernet/net/Session.hpp>
#include <hypernet/monitoring/Metrics.hpp>
#include <hypernet/buffer/RingBuffer.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/net/EventLoop.hpp>
#include <hypernet/net/SessionManager.hpp>
#include <hypernet/protocol/BuiltinOpcodes.hpp>
#include <sys/socket.h> // recvmsg
#include <sys/uio.h>    // iovec
#include <cerrno>
#include <cstring>
#include <new>

namespace hypernet::net
{
namespace
{
inline bool isNormalCloseReason(const char *reason) noexcept
{
    if (!reason)
        return false;

    return (std::strcmp(reason, "peer_close") == 0) ||
           (std::strcmp(reason, "worker_shutdown") == 0) ||
           (std::strcmp(reason, "epoll_hup") == 0) || (std::strcmp(reason, "epoll_rdhup") == 0) ||
           (std::strcmp(reason, "idle_timeout") == 0) ||
           (std::strcmp(reason, "heartbeat_timeout") == 0);
}
} // namespace

Session::Session(PrivateTag, SessionHandle handle, int ownerWorkerId, Socket &&socket,
                 SessionManager *ownerManager, std::size_t recvRingCapacity,
                 std::size_t sendRingCapacity) noexcept
    : handle_(handle), ownerWorkerId_(ownerWorkerId), socket_(std::move(socket)),
      ownerManager_(ownerManager), recvRingCapacity_(recvRingCapacity),
      sendRingCapacity_(sendRingCapacity)
{
    lastRxAt_ = std::chrono::steady_clock::now();
}

Session::~Session()
{
    // 규약상 이 destructor는 owner worker thread에서 실행되는 것이 정상입니다.
    // (SessionManager가 owner thread에서 erase하기 때문)
    if (socket_.isValid())
    {
        SLOG_WARN("Session", "DestructorCloseLeaked", "fd={} sid={} reason=LeakedSocket(BUG)",
                  socket_.nativeHandle(), handle_.id());
        socket_.close();
    }
}
std::shared_ptr<Session> Session::create(SessionHandle handle, int ownerWorkerId, Socket &&socket,
                                         SessionManager *ownerManager, std::size_t recvRingCapacity,
                                         std::size_t sendRingCapacity)
{
    // 생성 경로에서의 예외는 accept 핫패스를 죽이지 않도록 "로그 + 소켓 close + nullptr"로
    // 처리한다.
    try
    {
        auto s = std::make_shared<Session>(PrivateTag{}, handle, ownerWorkerId, std::move(socket),
                                           ownerManager, recvRingCapacity, sendRingCapacity);
        try
        {
            s->recvRing_ = std::make_unique<hypernet::buffer::RingBuffer>(recvRingCapacity);
            s->sendRing_ = std::make_unique<hypernet::buffer::RingBuffer>(sendRingCapacity);
        }
        catch (const std::exception &e)
        {
            SLOG_ERROR("Session", "AllocRingsFailed", "sid={} fd={} what='{}'", handle.id(),
                       s->socket_.nativeHandle(), e.what());
            s->socket_.close();
            s->state_ = SessionState::Closed;
            s->recvRing_.reset();
            s->sendRing_.reset();
            return {};
        }
        catch (...)
        {
            SLOG_ERROR("Session", "AllocRingsFailed", "sid={} fd={} what=unknown", handle.id(),
                       s->socket_.nativeHandle());
            s->socket_.close();
            s->state_ = SessionState::Closed;
            s->recvRing_.reset();
            s->sendRing_.reset();
            return {};
        }

        // 초기 등록 마스크는 SessionManager가 baseEpollMask_()로 등록하는 것을 전제로 한다.
        s->currentEpollMask_ = baseEpollMask_();

        return s;
    }
    catch (const std::exception &e)
    {
        SLOG_ERROR("Session", "CreateFailed", "sid={} what='{}'", handle.id(), e.what());
        if (socket.isValid())
        {
            socket.close();
        }
        return {};
    }
    catch (...)
    {
        SLOG_ERROR("Session", "CreateFailed", "sid={} what=unknown", handle.id());
        if (socket.isValid())
        {
            socket.close();
        }
        return {};
    }
}

void Session::handleEvent(EventLoop &loop, const EpollReactor::ReadyEvent &ev)
{

    if (state_ == SessionState::Closed)
        return;

    auto self = shared_from_this();
    (void)self;
    const std::uint32_t events = ev.events;

    // - EPOLLIN|EPOLLRDHUP 동시 발생 시, close/hup 경로보다 onReadable_()를 먼저 호출한다.
    // - ET(EPOLLET)에서는 onReadable_()가 반드시 EAGAIN까지 drain해야 하므로,
    //   커널 수신 버퍼에 남은 데이터를 먼저 모두 끌어온 뒤 종료 신호를 처리해야
    //   "send 후 즉시 close" 케이스에서 마지막 메시지 누락을 방지할 수 있다.
    if (events & EPOLLIN)
    {
        onReadable_(loop);
        // onReadable_()가 peer close(read==0)나 invalid frame 등으로 beginClose_()를 호출했을
        // 수 있다.
        if (state_ != SessionState::Connected)
        {
            return;
        }
    }

    // write는 read 이후에 처리(일반적으로 안전).
    // - 기존 정책: backlog 있을 때만 EPOLLOUT enable/disable은 onWritable_/setWriteInterest_가
    // 담당
    if (events & EPOLLOUT)
    {
        onWritable_(loop);
        if (state_ != SessionState::Connected)
        {
            return;
        }
    }

    // read/write drain 이후에도 남아있는 종료/에러 신호를 처리한다.
    // - EPOLLERR: ERROR 로그
    // - EPOLLRDHUP/EPOLLHUP: INFO/DEBUG 로그(정상 종료 신호로 취급)
    constexpr std::uint32_t kAfterDrainCloseMask = (EPOLLERR | EPOLLHUP | EPOLLRDHUP);
    if (events & kAfterDrainCloseMask)
    {
        onError_(loop, ev);
    }
}

bool Session::processRecvFrames_(EventLoop &loop) noexcept
{
    auto &framer = ownerManager_->framer();
    hypernet::protocol::MessageView msgView{};

    for (;;)
    {
        const auto r = framer.tryFrame(*recvRing_, msgView);

        if (r == hypernet::protocol::FrameResult::NeedMore)
        {
            return true;
        }

        if (r == hypernet::protocol::FrameResult::Invalid)
        {
            const char *reason = ownerManager_->lastFramerErrorReason();
            SLOG_WARN("Session", "InvalidFrameClose", "sid={} reason='{}'", handle_.id(),
                      reason ? reason : "(null)");
            beginClose_(loop, "framer_invalid", 0);
            return false;
        }

        // Framed
        ownerManager_->dispatchOnMessage(handle_, msgView);

        if (state_ != SessionState::Connected)
        {
            return false;
        }
    }
}

void Session::onReadable_(EventLoop &loop) noexcept
{
    // 수명/안전 규약: shared_from_this()로 self를 잡아 수명을 고정한다.
    auto self = shared_from_this();

    if (state_ != SessionState::Connected)
    {
        return;
    }

    if (!recvRing_)
    {
        SLOG_ERROR("Session", "RecvRingNull", "sid={} reason=OOM?", handle_.id());
        beginClose_(loop, "recv_ring_null", 0);
        return;
    }

    if (!ownerManager_)
    {
        SLOG_FATAL("Session", "OwnerManagerNull", "sid={} reason=BUG", handle_.id());
        std::abort();
    }

    const int fd = socket_.nativeHandle();

    // EPOLLET 규약: EAGAIN이 나올 때까지 반복해서 읽는다(drain).
    for (;;)
    {
        // 1. 링버퍼의 가용 공간(tail 부분)을 1~2개의 조각(iovec)으로 가져온다.
        ::iovec iov[2]{};
        const int iovcnt = recvRing_->writeIov(iov, recvRing_->freeSpace());

        // 링버퍼가 꽉 찬 경우
        if (iovcnt == 0)
        {
            SLOG_WARN("Session", "RecvOverflow", "sid={} fd={} cap={} size={}", handle_.id(), fd,
                      recvRing_->capacity(), recvRing_->size());
            beginClose_(loop, "recv_overflow", 0);
            return;
        }

        // 2. recvmsg를 사용하여 커널에서 링버퍼 메모리로 직접 데이터를 수신한다. (중간 복사
        // 제거)
        ::msghdr msg{};
        msg.msg_iov = iov;
        msg.msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(iovcnt);

        const ::ssize_t n = ::recvmsg(fd, &msg, 0);

        if (n > 0)
        {
            const std::size_t bytes = static_cast<std::size_t>(n);

            //  실제로 수신한 바이트 수만큼 링버퍼의 tail 포인터를 이동시킨다.
            recvRing_->commitWrite(bytes);
            touchRx_();
            if (!processRecvFrames_(loop))
            {
                return;
            }
            continue;
        }

        if (n == 0)
        {
            // 상대방이 연결을 종료함 (FIN 수신)
            SLOG_INFO("Session", "PeerClosed", "sid={} fd={}", handle_.id(), fd);
            beginClose_(loop, "peer_close", 0);
            return;
        }

        // n < 0 : 에러 처리
        if (errno == EINTR)
        {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 더 이상 읽을 데이터가 없음 (Edge-Triggered 완료)
            break;
        }

        // 실제 소켓 에러
        SLOG_ERROR("Session", "RecvFailed", "sid={} fd={} errno={} msg='{}'", handle_.id(), fd,
                   errno, std::strerror(errno));
        beginClose_(loop, "recv_error", errno);
        return;
    }
}

void Session::onWritable_(EventLoop &loop) noexcept
{
    if (state_ != SessionState::Connected)
    {
        return;
    }

    if (!sendRing_)
    {
        SLOG_ERROR("Session", "SendRingMissing", "sid={} fd={}", handle_.id(),
                   socket_.nativeHandle());
        beginClose_(loop, "send_ring_missing", 0);
        return;
    }

    if (!flushSend_(loop))
    {
        return; // flush 과정에서 close됨
    }

    // backlog 있을 때만 EPOLLOUT ON, 비면 OFF (Contract 고정)
    setWriteInterest_(loop, !sendRing_->empty());
}

void Session::onError_(EventLoop &loop, const EpollReactor::ReadyEvent &ev) noexcept
{
    if (state_ != SessionState::Connected)
    {
        return;
    }
    const std::uint32_t events = ev.events;

    if (events & EPOLLERR)
    {
        SLOG_ERROR("Session", "EpollError", "sid={} fd={} events=0x{:x}", handle_.id(),
                   nativeHandle(), events);
        beginClose_(loop, "epoll_err", /*err=*/0);
        return;
    }

    if (events & (EPOLLHUP | EPOLLRDHUP))
    {
        SLOG_INFO("Session", "EpollHup", "sid={} fd={} events=0x{:x}", handle_.id(), nativeHandle(),
                  events);

        beginClose_(loop, (events & EPOLLRDHUP) ? "epoll_rdhup" : "epoll_hup", /*err=*/0);
        return;
    }

    SLOG_WARN("Session", "EpollUnknownCloseSignal", "sid={} fd={} events=0x{:x}", handle_.id(),
              nativeHandle(), events);

    beginClose_(loop, "epoll_unknown", /*err=*/0);
}

void Session::beginClose_(EventLoop &loop, const char *reason, int err) noexcept
{
    // 스레딩/소유권 규약(중요):
    // - fd remove/close, 상태 전이는 오직 owner worker thread에서만 수행
    if (!loop.isInOwnerThread())
    {
        SLOG_FATAL("Session", "BeginCloseWrongThread", "sid={}", handle_.id());
        std::abort();
    }

    if (state_ == SessionState::Closed)
    {
        return;
    }
    if (state_ == SessionState::Closing)
    {
        return; // idempotent
    }

    // self 보유: 아래에서 manager->erase로 인해 refcount가 0이 될 수 있으므로
    // 이 함수 종료까지 객체 수명을 보장한다(UAF 방지).
    auto self = shared_from_this();
    (void)self;

    state_ = SessionState::Closing;

    const int fd = socket_.nativeHandle();
    SLOG_INFO("Session", "BeginClose", "sid={} fd={} reason='{}' err={} err_str='{}'", handle_.id(),
              fd, (reason ? reason : "(null)"), err, (err != 0 ? std::strerror(err) : "ok"));

    // epoll 등록 해제는 close 전에 수행(지연 이벤트/UAF 방지)
    if (fd >= 0)
    {
        (void)loop.removeFd(fd);
    }

    socket_.close();
    state_ = SessionState::Closed;
    if (err != 0 || !isNormalCloseReason(reason))
    {
        hypernet::monitoring::engineMetrics().onError();
    }

    // SessionManager에 “회수/정리” 통지 (worker-local 컨테이너에서 제거)
    if (ownerManager_)
    {
        ownerManager_->onSessionClosed(handle_.id());
    }
}

void Session::closeFromManager_(EventLoop &loop, const char *reason) noexcept
{
    closeFromManager_(loop, reason, 0);
}
void Session::closeFromManager_(EventLoop &loop, const char *reason, int err) noexcept
{
    // 워커 종료 시 강제 정리 경로:
    // - owner thread에서만 수행
    // - manager 콜백(erase) 호출 금지 (SessionManager가 마지막에 clear/erase)
    if (!loop.isInOwnerThread())
    {
        SLOG_FATAL("Session", "CloseFromManagerWrongThread", "sid={}", handle_.id());
        std::abort();
    }

    if (state_ == SessionState::Closed)
    {
        return;
    }

    state_ = SessionState::Closing;

    const int fd = socket_.nativeHandle();
    SLOG_INFO("Session", "CloseFromManager", "sid={} fd={} reason='{}' err={} err_str='{}'",
              handle_.id(), fd, (reason ? reason : "(null)"), err,
              (err != 0 ? std::strerror(err) : "ok"));

    if (fd >= 0)
    {
        (void)loop.removeFd(fd);
    }

    socket_.close();
    state_ = SessionState::Closed;
}

bool Session::enqueueSendNoFlush_(EventLoop &loop, const void *data, std::size_t len) noexcept
{
    if (!loop.isInOwnerThread())
    {
        SLOG_FATAL("Session", "EnqueueWrongThread", "api=enqueueSendNoFlush sid={}", handle_.id());
        std::abort();
    }

    if (state_ != SessionState::Connected)
    {
        return false;
    }

    if (!data || len == 0)
    {
        return true;
    }

    if (!sendRing_)
    {
        SLOG_ERROR("Session", "SendRingMissing", "sid={} fd={}", handle_.id(),
                   socket_.nativeHandle());
        beginClose_(loop, "send_ring_missing", 0);
        return false;
    }

    const std::size_t free = sendRing_->freeSpace();
    if (free < len)
    {
        SLOG_ERROR("Session", "SendOverflowClose",
                   "sid={} fd={} cap={} size={} free={} enqueue_len={}", handle_.id(),
                   socket_.nativeHandle(), sendRing_->capacity(), sendRing_->size(), free, len);
        beginClose_(loop, "send_overflow", 0);
        return false;
    }

    const auto *bytes = static_cast<const std::byte *>(data);
    const std::size_t written = sendRing_->write(bytes, len);
    if (written != len)
    {
        SLOG_FATAL("Session", "SendRingWriteMismatch", "sid={} fd={} want={} wrote={}",
                   handle_.id(), socket_.nativeHandle(), len, written);
        beginClose_(loop, "send_ring_write_mismatch", 0);
        return false;
    }

    return state_ == SessionState::Connected;
}

bool Session::enqueuePacketU16Coalesced(EventLoop &loop, const std::uint8_t lenHdr4[4],
                                        const std::uint8_t opHdr2[2], const void *body,
                                        std::size_t bodyLen) noexcept
{
    if (!loop.isInOwnerThread())
    {
        SLOG_FATAL("Session", "EnqueuePacketWrongThread", "api=enqueuePacketU16Coalesced sid={}",
                   handle_.id());
        std::abort();
    }

    if (state_ != SessionState::Connected)
        return false;

    if (!sendRing_)
    {
        beginClose_(loop, "send_ring_missing", 0);
        return false;
    }

    // 방어: bodyLen > 0 인데 body가 null이면 프로그래밍 에러 취급(혹은 close 정책)
    if (bodyLen > 0 && body == nullptr)
    {
        SLOG_FATAL("Session", "EnqueueInvalidBody", "sid={} body_len={} body=null", handle_.id(),
                   bodyLen);
        beginClose_(loop, "send_invalid_body", 0);
        return false;
    }

    const int fd = socket_.nativeHandle();

    struct Seg
    {
        const std::uint8_t *p;
        std::size_t n;
    };

    auto consumeRing = [&](std::size_t nbytes) noexcept
    {
        while (nbytes > 0 && sendRing_ && !sendRing_->empty())
        {
            const auto v = sendRing_->readView(nbytes);
            if (v.empty())
                break;
            nbytes -= v.size();
        }
    };

    auto enqueueRemainder = [&](std::size_t skip, const Seg *segs, int cnt) noexcept -> bool
    {
        for (int i = 0; i < cnt; ++i)
        {
            if (segs[i].n == 0)
                continue;

            if (skip >= segs[i].n)
            {
                skip -= segs[i].n;
                continue;
            }

            const std::uint8_t *p = segs[i].p + skip;
            const std::size_t rem = segs[i].n - skip;
            skip = 0;

            if (!enqueueSendNoFlush_(loop, p, rem))
                return false;
        }
        return true;
    };

    // 새 메시지 세그먼트 구성(주의: bodyLen==0이면 body 세그는 n=0)
    const Seg segs[] = {
        {lenHdr4, 4},
        {opHdr2, 2},
        {static_cast<const std::uint8_t *>(body), bodyLen},
    };

    for (;;)
    {
        ::iovec iov[5]{};
        int iovcnt = 0;
        std::size_t ringAvail = 0;

        // 1) 기존 backlog (FIFO) 먼저
        if (!sendRing_->empty())
        {
            ::iovec ringIov[2]{};
            const int rcnt = sendRing_->peekIov(ringIov, sendRing_->available());
            for (int i = 0; i < rcnt; ++i)
            {
                if (ringIov[i].iov_len == 0)
                    continue;
                iov[iovcnt++] = ringIov[i];
                ringAvail += ringIov[i].iov_len;
            }
        }

        // 2) 새 메시지( lenHdr4 + opHdr2 + body ) 추가
        for (int i = 0; i < 3; ++i)
        {
            if (segs[i].n == 0)
                continue;
            // segs[2]는 bodyLen>0이면 body!=null 이미 보장
            iov[iovcnt].iov_base = const_cast<std::uint8_t *>(segs[i].p);
            iov[iovcnt].iov_len = segs[i].n;
            ++iovcnt;
        }

        ::msghdr m{};
        m.msg_iov = iov;
        m.msg_iovlen = static_cast<decltype(m.msg_iovlen)>(iovcnt);

        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        const ::ssize_t n = ::sendmsg(fd, &m, flags);

        if (n > 0)
        {
            std::size_t sent = static_cast<std::size_t>(n);

            // (A) 기존 링버퍼 소모
            const std::size_t sentFromRing = (sent <= ringAvail) ? sent : ringAvail;
            if (sentFromRing > 0)
            {
                consumeRing(sentFromRing);
                sent -= sentFromRing;
            }

            // (B) 새 메시지에서 얼마나 보냈는지
            const std::size_t newTotal = 4 + 2 + bodyLen;

            if (sent < newTotal)
            {
                // 일부만 보냄 -> 남은 것만 enqueue 후 flush 1회
                if (!enqueueRemainder(sent, segs, 3))
                    return false;
                if (!flushSend_(loop))
                    return false;
            }
            else
            {
                // 새 메시지까지 다 보냄 -> backlog 있으면 best-effort flush 1회
                (void)flushSend_(loop);
            }

            setWriteInterest_(loop, !sendRing_->empty());
            return state_ == SessionState::Connected;
        }

        if (n == 0)
        {
            beginClose_(loop, "send_zero", 0);
            return false;
        }

        // n < 0
        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 못 보냄 -> 새 메시지 전체 큐잉 후 EPOLLOUT
            if (!enqueueRemainder(0, segs, 3))
                return false;

            setWriteInterest_(loop, true);
            return state_ == SessionState::Connected;
        }

        beginClose_(loop, "send_failed", errno);
        return false;
    }
}

bool Session::flushSend_(EventLoop &loop) noexcept
{

    // 1. 상태 체크: 연결된 상태가 아니면 보낼 필요 없음
    if (state_ != SessionState::Connected)
    {
        return false;
    }

    const int fd = socket_.nativeHandle();

    // EPOLLET(Edge-Triggered) 규약: EAGAIN이 나올 때까지 최대한 쏟아낸다(drain).
    for (;;)
    {
        // 2. 보낼 데이터가 없으면 루프 종료
        if (!sendRing_ || sendRing_->empty())
        {
            break;
        }

        // 3. 링버퍼의 데이터 구간(head부터)을 1~2개의 조각(iovec)으로 가져온다.
        ::iovec iov[2]{};
        const int iovcnt = sendRing_->peekIov(iov, sendRing_->available());
        if (iovcnt == 0)
        {
            break;
        }

        // 4. writev를 사용하여 여러 조각의 메모리를 커널 송신 버퍼로 직접 쏜다. (Zero-copy
        // 송신)
        const ::ssize_t n = ::writev(fd, iov, iovcnt);

        if (n > 0)
        {
            // 성공적으로 전송된 바이트 수
            std::size_t totalSent = static_cast<std::size_t>(n);

            // 5. [중요] 보낸 바이트 수만큼 링버퍼에서 실제로 제거(head 이동)
            // readView는 한 번 호출 시 연속된 구간 하나만 반환하므로,
            // 2개 조각(wrap 발생)을 모두 소비하려면 남은 바이트가 없을 때까지 반복 호출합니다.
            while (totalSent > 0)
            {
                const auto consumed = sendRing_->readView(totalSent);
                if (consumed.empty())
                {
                    // 논리적으로 발생하면 안 되는 상황 (RingBuffer 상태 불일치 시 발생 가능)
                    break;
                }
                totalSent -= consumed.size();
            }

            // 전송이 성공했으므로 다음 drain 루프로 계속 진행
            continue;
        }

        // n == 0 처리
        if (n == 0)
        {
            SLOG_ERROR("Session", "WritevZero", "sid={} fd={}", handle_.id(), fd);
            beginClose_(loop, "send_zero", 0);
            return false;
        }

        // n < 0: 에러 처리
        if (errno == EINTR)
        {
            continue; // 인터럽트는 재시도
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 커널 송신 버퍼가 꽉 참: 나중에 EPOLLOUT 이벤트가 오면 다시 시도해야 함
            break;
        }

        // 실제 소켓 에러 (연결 끊김 등)
        const int e = errno;
        SLOG_ERROR("Session", "WritevFailed", "sid={} fd={} errno={} msg='{}'", handle_.id(), fd, e,
                   std::strerror(e));
        beginClose_(loop, "send_failed", e);
        return false;
    }

    return state_ == SessionState::Connected;
}

void Session::setWriteInterest_(EventLoop &loop, bool enable) noexcept
{
    if (state_ != SessionState::Connected)
    {
        return;
    }

    const std::uint32_t desired =
        enable ? (baseEpollMask_() | static_cast<std::uint32_t>(EpollReactor::Event::Write))
               : baseEpollMask_();

    if (currentEpollMask_ == desired)
    {
        return; // 중복 epoll_ctl 방지
    }

    const int fd = socket_.nativeHandle();
    if (fd < 0)
    {
        return;
    }

    if (!loop.updateFd(fd, desired))
    {
        const int e = errno;
        SLOG_ERROR("Session", "EpollModFailed", "sid={} fd={} events=0x{:x} errno={} msg='{}'",
                   handle_.id(), fd, desired, e, std::strerror(e));
        beginClose_(loop, "epoll_mod_failed", e);
        return;
    }

    currentEpollMask_ = desired;

    SLOG_DEBUG("Session", "WriteInterest", "enable={} sid={} fd={} events=0x{:x}", enable,
               handle_.id(), fd, desired);
}

void Session::touchRx_() noexcept
{
    lastRxAt_ = std::chrono::steady_clock::now();
}

void Session::startTimeouts_(EventLoop &loop, std::uint32_t idleTimeoutMs,
                             std::uint32_t heartbeatIntervalMs) noexcept
{
    // owner thread에서만 호출되는 전제(SessionManager::onAccepted 경로)
    idleTimeoutMs_ = idleTimeoutMs;
    heartbeatIntervalMs_ = heartbeatIntervalMs;

    lastRxAt_ = std::chrono::steady_clock::now();

    if (idleTimeoutMs_ > 0)
    {
        armIdleTimerAfter_(loop, std::chrono::milliseconds(idleTimeoutMs_));
    }
    if (heartbeatIntervalMs_ > 0)
    {
        armHeartbeatTimerAfter_(loop, std::chrono::milliseconds(heartbeatIntervalMs_));
    }
}

void Session::armIdleTimerAfter_(EventLoop &loop, std::chrono::milliseconds delay) noexcept
{
    if (idleTimeoutMs_ == 0 || state_ != SessionState::Connected || idleTimerArmed_)
        return;

    idleTimerArmed_ = true;

    // 세션이 먼저 죽어도 안전하게: weak_ptr로 보호
    std::weak_ptr<Session> weak = weak_from_this();
    auto *loopPtr = &loop;

    // EventLoop에 timer API가 이미 존재한다고 가정(기존 TimerWheel 기반 구조)
    // :contentReference[oaicite:6]{index=6}
    (void)loop.addTimer(delay,
                        [weak, loopPtr]()
                        {
                            if (auto self = weak.lock())
                            {
                                self->onIdleTimer_(*loopPtr);
                            }
                        });
}

void Session::onIdleTimer_(EventLoop &loop) noexcept
{
    idleTimerArmed_ = false;

    if (idleTimeoutMs_ == 0 || state_ != SessionState::Connected)
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRxAt_);

    if (elapsed.count() >= static_cast<long long>(idleTimeoutMs_))
    {
        beginClose_(loop, "idle_timeout", 0);
        return;
    }

    // 남은 시간만큼만 다시 한 번 예약(활동 때마다 재예약하지 않는 핵심 포인트)
    auto remaining = std::chrono::milliseconds(idleTimeoutMs_) - elapsed;
    if (remaining.count() <= 0)
        remaining = std::chrono::milliseconds(1);

    armIdleTimerAfter_(loop, remaining);
}

void Session::armHeartbeatTimerAfter_(EventLoop &loop, std::chrono::milliseconds delay) noexcept
{
    if (heartbeatIntervalMs_ == 0 || state_ != SessionState::Connected || heartbeatTimerArmed_)
        return;

    heartbeatTimerArmed_ = true;

    std::weak_ptr<Session> weak = weak_from_this();
    auto *loopPtr = &loop;

    (void)loop.addTimer(delay,
                        [weak, loopPtr]()
                        {
                            if (auto self = weak.lock())
                            {
                                self->onHeartbeatTimer_(*loopPtr);
                            }
                        });
}

void Session::onHeartbeatTimer_(EventLoop &loop) noexcept
{
    heartbeatTimerArmed_ = false;

    if (heartbeatIntervalMs_ == 0 || state_ != SessionState::Connected)
        return;

    constexpr int kMaxMissed = 2;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRxAt_);

    const auto interval = std::chrono::milliseconds(heartbeatIntervalMs_);
    const auto timeout = interval * kMaxMissed;

    // 2*interval 이상 RX가 없으면 종료
    if (elapsed >= timeout)
    {
        beginClose_(loop, "heartbeat_timeout", 0);
        return;
    }

    // interval 이상 조용했으면 ping 1회 전송 (best-effort)
    if (elapsed >= interval)
    {
        if (ownerManager_)
        {
            (void)ownerManager_->sendPacketU16(handle_.id(), hypernet::protocol::kOpcodePing,
                                               nullptr, 0);
        }

        // 다음 체크는 "timeout까지 남은 시간"과 "interval" 중 더 빠른 쪽
        auto remainToTimeout = timeout - elapsed;
        auto next = (remainToTimeout < interval) ? remainToTimeout : interval;
        if (next <= std::chrono::milliseconds(0))
            next = std::chrono::milliseconds(1);

        armHeartbeatTimerAfter_(loop, next);
        return;
    }

    // 아직 interval만큼 조용하지 않음 -> 남은 시간만큼 후에 다시 체크
    auto remaining = interval - elapsed;
    if (remaining <= std::chrono::milliseconds(0))
        remaining = std::chrono::milliseconds(1);

    armHeartbeatTimerAfter_(loop, remaining);
}
} // namespace hypernet::net
