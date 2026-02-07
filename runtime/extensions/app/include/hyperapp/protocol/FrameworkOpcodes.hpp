// FILE: runtime/extensions/game/include/hyperapp/protocol/FrameworkOpcodes.hpp
#pragma once
#include <cstdint>

namespace hyperapp::protocol
{
// 프레임워크가 기본 제공하는 “멤버십/이동” 이벤트용 opcode.
// 프로젝트에서 충돌 나면 여기 숫자만 바꾸면 됨.
inline constexpr std::uint16_t kOpcodeEnterNotify = 10001;
inline constexpr std::uint16_t kOpcodeLeaveNotify = 10002;
inline constexpr std::uint16_t kOpcodeTopicMoveAck = 10003;

// [NEW] 프레임워크 예약 영역(앱 opcode가 침범 못하게)
inline constexpr std::uint16_t kFrameworkOpcodeBegin = 10000;
inline constexpr std::uint16_t kFrameworkOpcodeEnd = 11000;

inline constexpr bool isFrameworkReserved(std::uint16_t opcode) noexcept
{
    return (opcode >= kFrameworkOpcodeBegin) && (opcode < kFrameworkOpcodeEnd);
}

} // namespace hyperapp::protocol
