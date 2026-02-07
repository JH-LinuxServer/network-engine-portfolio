#pragma once
#include <cstdint>

namespace hypernet::protocol
{
inline constexpr std::uint16_t kOpcodePing = 0xFFFE;
inline constexpr std::uint16_t kOpcodePong = 0xFFFF;
} // namespace hypernet::protocol