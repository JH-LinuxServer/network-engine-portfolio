#pragma once
#include <cstdint>

namespace hyperapp
{
enum class ConnState : std::uint8_t
{
    Connected = 0,    // accept 직후
    Handshaked = 1,   // 버전/헬로 완료
    Authed = 2,       // 인증 완료
    Joined = 3, // 스코프/토픽 진입 완료
    Active = 4,       // 인게임
    Closing = 5
};

inline constexpr std::uint32_t stateBit(ConnState s) noexcept
{
    return 1u << static_cast<std::uint32_t>(s);
}
} // namespace hyperapp