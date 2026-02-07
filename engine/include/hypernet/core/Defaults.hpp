#pragma once
#include <cstddef>
#include <cstdint>

namespace hypernet::core::defaults
{

// ===== Worker timer / epoll =====
inline constexpr std::uint32_t kTickResolutionMs = 10;
inline constexpr std::size_t kTimerSlots = 1024;
inline constexpr int kMaxEpollEvents = 64;

// ===== Buffer pool =====
inline constexpr std::size_t kBufferBlockSize = 4096;
inline constexpr std::size_t kBufferBlockCount = 1024;

// ===== Per-session rings =====
inline constexpr std::size_t kRecvRingCapacity = 64 * 1024;
inline constexpr std::size_t kSendRingCapacity = 64 * 1024;

// ===== Protocol policy =====
inline constexpr std::uint32_t kMaxPayloadLen = 1024U * 1024U; // 1 MiB

// ===== Listener =====
inline constexpr int kListenBacklog = 128;

} // namespace hypernet::core::defaults
