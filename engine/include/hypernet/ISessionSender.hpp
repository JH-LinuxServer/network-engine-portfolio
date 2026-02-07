#pragma once

#include <cstddef>
#include <cstdint>

namespace hypernet
{

/// 앱이 세션에 패킷(opcode+body)을 송신하기 위한 엔진 공용 인터페이스입니다.
/// 최종 계약: [len(4)][opcode(2)][body] 만 허용합니다.
class ISessionSender
{
  public:
    using SessionId = std::uint64_t;
    virtual ~ISessionSender() = default;

    virtual bool sendPacketU16(SessionId id, std::uint16_t opcode, const void *body,
                               std::size_t bodyLen) noexcept = 0;
};

} // namespace hypernet
