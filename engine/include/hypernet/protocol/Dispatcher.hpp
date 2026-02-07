#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include <hypernet/SessionHandle.hpp>
#include <hypernet/protocol/MessageView.hpp>
#include <hypernet/protocol/Endian.hpp>

namespace hypernet::protocol
{

struct MessageHeader
{
    /// Wire framing SSOT:
    ///   [Length:u32_be] + [Opcode:u16_be] + [Body...]
    /// - Length == (Opcode + Body) in bytes (length field itself 제외)
    static constexpr std::size_t kLengthFieldBytes = 4;
    static constexpr std::size_t kOpcodeFieldBytes = 2;
    static constexpr std::size_t kWireBytes = kLengthFieldBytes + kOpcodeFieldBytes;
    static constexpr std::uint64_t kMaxPayloadLenU64 = 0xFFFF'FFFFULL;

    std::uint32_t payloadLen{0}; // host-order: opcode+body bytes
    std::uint16_t opcode{0};     // host-order

    void encodeLen(std::uint8_t out[kLengthFieldBytes]) const noexcept
    {
        hypernet::protocol::storeU32Be(payloadLen, out);
    }
    void encodeOpcode(std::uint8_t out[kOpcodeFieldBytes]) const noexcept
    {
        hypernet::protocol::storeU16Be(opcode, out);
    }

    [[nodiscard]] static constexpr std::size_t payloadLenForBody(std::size_t bodyLen) noexcept
    {
        return kOpcodeFieldBytes + bodyLen;
    }
};

class Dispatcher
{
  public:
    using OpCode = std::uint16_t;
    using Handler =
        std::function<void(hypernet::SessionHandle, const hypernet::protocol::MessageView &)>;

    /// 핸들러 등록(중복 opcode는 거부)
    /// @return true if inserted, false if opcode already exists or handler invalid
    bool registerHandler(OpCode opcode, Handler handler) noexcept
    {
        if (!handler)
        {
            return false;
        }
        auto [it, inserted] = handlers_.emplace(opcode, std::move(handler));
        return inserted;
    }

    bool unregisterHandler(OpCode opcode) noexcept { return handlers_.erase(opcode) > 0; }

    void clear() noexcept { handlers_.clear(); }

    [[nodiscard]] std::size_t handlerCount() const noexcept { return handlers_.size(); }

    /// @return true if handled, false if unknown opcode
    bool dispatch(OpCode opcode, hypernet::SessionHandle session,
                  const hypernet::protocol::MessageView &body) const
    {
        auto it = handlers_.find(opcode);
        if (it == handlers_.end())
        {
            return false;
        }
        it->second(session, body);
        return true;
    }

  private:
    std::unordered_map<OpCode, Handler> handlers_;
};

} // namespace hypernet::protocol
