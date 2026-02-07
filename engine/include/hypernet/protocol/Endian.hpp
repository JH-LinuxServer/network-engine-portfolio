#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <cstddef> // std::byte
#include <cstdint>
#include <cstring>
#include <bit>

namespace hypernet::protocol
{
inline void storeU16Be(std::uint16_t v, std::uint8_t out[2]) noexcept
{
    out[0] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    out[1] = static_cast<std::uint8_t>((v >> 0) & 0xFF);
}

inline void storeU32Be(std::uint32_t v, std::uint8_t out[4]) noexcept
{
    out[0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    out[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    out[2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    out[3] = static_cast<std::uint8_t>((v >> 0) & 0xFF);
}

inline std::uint16_t loadU16Be(const std::uint8_t *p) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
                                      (static_cast<std::uint16_t>(p[1]) << 0));
}

inline std::uint32_t loadU32Be(const std::uint8_t *p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | (static_cast<std::uint32_t>(p[3]) << 0);
}

// LengthPrefixFramer가 std::byte를 쓰므로 overload 하나 더 제공
inline std::uint32_t loadU32Be(const std::byte *p) noexcept
{
    const auto b0 = static_cast<std::uint8_t>(p[0]);
    const auto b1 = static_cast<std::uint8_t>(p[1]);
    const auto b2 = static_cast<std::uint8_t>(p[2]);
    const auto b3 = static_cast<std::uint8_t>(p[3]);
    return (static_cast<std::uint32_t>(b0) << 24) | (static_cast<std::uint32_t>(b1) << 16) |
           (static_cast<std::uint32_t>(b2) << 8) | (static_cast<std::uint32_t>(b3) << 0);
}
// [추가] 64-bit Big-Endian 저장
inline void storeU64Be(std::uint64_t v, std::uint8_t out[8]) noexcept
{
    out[0] = static_cast<std::uint8_t>((v >> 56) & 0xFF);
    out[1] = static_cast<std::uint8_t>((v >> 48) & 0xFF);
    out[2] = static_cast<std::uint8_t>((v >> 40) & 0xFF);
    out[3] = static_cast<std::uint8_t>((v >> 32) & 0xFF);
    out[4] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    out[5] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    out[6] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    out[7] = static_cast<std::uint8_t>((v >> 0) & 0xFF);
}

// [추가] 64-bit Big-Endian 로드
inline std::uint64_t loadU64Be(const std::uint8_t *p) noexcept
{
    return (static_cast<std::uint64_t>(p[0]) << 56) | (static_cast<std::uint64_t>(p[1]) << 48) |
           (static_cast<std::uint64_t>(p[2]) << 40) | (static_cast<std::uint64_t>(p[3]) << 32) |
           (static_cast<std::uint64_t>(p[4]) << 24) | (static_cast<std::uint64_t>(p[5]) << 16) |
           (static_cast<std::uint64_t>(p[6]) << 8) | (static_cast<std::uint64_t>(p[7]) << 0);
}
} // namespace hypernet::protocol
