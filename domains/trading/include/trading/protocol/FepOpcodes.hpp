#pragma once

#include <cstdint>
#include <trading/protocol/OpcodePolicy.hpp>

namespace trading::protocol
{
// -----------------------------------------------------------------------------
// FEP-lite opcodes (SSOT)
// -----------------------------------------------------------------------------

// Handshake & Auth
inline constexpr std::uint16_t kOpcodeRoleHelloReq = 20007;
inline constexpr std::uint16_t kOpcodeRoleHelloAck = 20008;

// ---- Benchmark / Performance ----
static constexpr std::uint16_t kOpcodePerfPing = 20050;
static constexpr std::uint16_t kOpcodePerfPong = 20051;

// -----------------------------------------------------------------------------
// Compile-time policy guard
// -----------------------------------------------------------------------------

static_assert(isValidTradingOpcode(kOpcodeRoleHelloReq), "OpcodePolicy violation: RoleHelloReq");
static_assert(isValidTradingOpcode(kOpcodeRoleHelloAck), "OpcodePolicy violation: RoleHelloAck");
static_assert(isValidTradingOpcode(kOpcodePerfPing), "OpcodePolicy violation: PerfPing");
static_assert(isValidTradingOpcode(kOpcodePerfPong), "OpcodePolicy violation: PerfPong");
} // namespace trading::protocol