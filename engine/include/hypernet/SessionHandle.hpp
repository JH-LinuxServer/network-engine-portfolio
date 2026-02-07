#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <hypernet/ISessionSender.hpp>
#include <hypernet/protocol/MessageView.hpp>
namespace hypernet
{

/// 클라이언트 세션을 식별하는 (opaque) 핸들입니다.
///
/// ===== 수명/스레딩 규약(중요) =====
/// - SessionHandle은 "세션이 살아있는 동안"에 한해 유효한 핸들로 취급합니다.
/// - 특히 send()/sendFramed()는 엔진 규약상 **해당 세션의 owner worker thread에서만**
///   성공하도록 구현됩니다. (cross-thread 직접 송신 금지)
/// - onSessionEnd 이후에 저장해둔 handle로 send를 호출해도 실패(false)하도록 의도합니다.
///   (UAF 방지를 위해 내부 sender는 weak_ptr로 보관)
class SessionHandle
{
  public:
    using Id = std::uint64_t;

    SessionHandle() = default;
    explicit SessionHandle(Id id) : id_(id) {}

    /// 엔진 내부에서만 주로 사용하는 생성자:
    /// - sender는 weak_ptr로 잡아 수명 종료 후에도 안전하게 실패하도록 한다.
    SessionHandle(Id id, int ownerWorkerId, std::weak_ptr<ISessionSender> sender) noexcept
        : id_(id), ownerWorkerId_(ownerWorkerId), sender_(std::move(sender))
    {
    }

    [[nodiscard]] Id id() const noexcept { return id_; }
    [[nodiscard]] bool isValid() const noexcept { return id_ != 0; }
    explicit operator bool() const noexcept { return isValid(); }

    /// 디버깅/로그용: owner worker id (엔진이 주입)
    [[nodiscard]] int ownerWorkerId() const noexcept { return ownerWorkerId_; }

    bool sendLocalPacketU16(std::uint16_t opcode, const void *body,
                            std::size_t bodyLen) const noexcept
    {
        if (!isValid())
            return false;
        auto s = sender_.lock();
        if (!s)
            return false;
        return s->sendPacketU16(id_, opcode, body, bodyLen);
    }

    bool sendLocalPacketU16(std::uint16_t opcode,
                            const hypernet::protocol::MessageView &body) const noexcept
    {
        return sendLocalPacketU16(opcode, body.data(), body.size());
    }

    bool sendPacketU16(std::uint16_t opcode, const void *body, std::size_t bodyLen) const noexcept
    {
        if (!isValid())
            return false;
        auto s = sender_.lock();
        if (!s)
            return false;
        return s->sendPacketU16(id_, opcode, body, bodyLen);
    }

    bool sendPacketU16(std::uint16_t opcode,
                       const hypernet::protocol::MessageView &body) const noexcept
    {
        return sendPacketU16(opcode, body.data(), body.size());
    }
    [[nodiscard]] static constexpr int ownerWorkerFromId(SessionHandle::Id sid) noexcept
    {
        // SessionManager::nextSessionId_() 규약: (ownerWid<<32) | local
        return static_cast<int>((sid >> 32) & 0xFFFFFFFFull);
    }

  private:
    Id id_{0};
    int ownerWorkerId_{-1};
    std::weak_ptr<ISessionSender> sender_{};
};

} // namespace hypernet
