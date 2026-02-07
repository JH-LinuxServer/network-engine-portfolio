#pragma once

#include <cstdint>

#include <hyperapp/protocol/FrameworkOpcodes.hpp>
#include <hypernet/protocol/BuiltinOpcodes.hpp>

namespace trading::protocol
{
// -----------------------------------------------------------------------------
// Trading(App) Opcode Policy
// -----------------------------------------------------------------------------
// - 앱 opcode는 이 범위를 사용한다. (프로젝트 진행 중 필요 시 변경 가능)
// - 프레임워크 예약(10000~11000) 및 엔진 내장(Ping/Pong)과 충돌 금지.
// -----------------------------------------------------------------------------
inline constexpr std::uint16_t kTradingOpcodeBegin = 20000;
inline constexpr std::uint16_t kTradingOpcodeEnd = 30000;

[[nodiscard]] inline constexpr bool isInTradingRange(std::uint16_t opcode) noexcept
{
    return (opcode >= kTradingOpcodeBegin) && (opcode < kTradingOpcodeEnd);
}

[[nodiscard]] inline constexpr bool isFrameworkReservedOpcode(std::uint16_t opcode) noexcept
{
    return hyperapp::protocol::isFrameworkReserved(opcode);
}

[[nodiscard]] inline constexpr bool isEngineBuiltinOpcode(std::uint16_t opcode) noexcept
{
    return (opcode == hypernet::protocol::kOpcodePing) ||
           (opcode == hypernet::protocol::kOpcodePong);
}

// trading 앱이 사용하는 opcode로 "유효"한가?
// - (정책) trading 범위 안이어야 하며
// - (정책) 프레임워크 예약/엔진 내장과 충돌하면 안 된다.
[[nodiscard]] inline constexpr bool isValidTradingOpcode(std::uint16_t opcode) noexcept
{
    return isInTradingRange(opcode) && !isFrameworkReservedOpcode(opcode) &&
           !isEngineBuiltinOpcode(opcode);
}

} // namespace trading::protocol