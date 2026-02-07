#pragma once

#include <hyperapp/protocol/FrameworkOpcodes.hpp>
#include <hyperapp/protocol/PacketWriter.hpp>

#include <cstddef>
#include <cstdint>

namespace hyperapp::protocol
{

struct LeaveNotifyPkt
{
    static constexpr std::uint16_t kOpcode = kOpcodeLeaveNotify;
    static constexpr std::size_t kReserveBytes = 8;

    std::uint64_t sid{0};

    void write(PacketWriter &w) const noexcept { w.writeU64Be(sid); }
};

struct EnterNotifyPkt
{
    static constexpr std::uint16_t kOpcode = kOpcodeEnterNotify;
    static constexpr std::size_t kReserveBytes = 8;

    std::uint64_t sid{0};

    void write(PacketWriter &w) const noexcept { w.writeU64Be(sid); }
};

struct TopicMoveAckPkt
{
    static constexpr std::uint16_t kOpcode = kOpcodeTopicMoveAck;
    static constexpr std::size_t kReserveBytes = 8;

    std::uint32_t world{0};
    std::uint32_t channel{0};

    void write(PacketWriter &w) const noexcept
    {
        w.writeU32Be(world);
        w.writeU32Be(channel);
    }
};

// -----------------------------
// 기존 API 유지: builder 함수
// -----------------------------
inline PacketWriter buildLeaveNotify(std::uint64_t sid)
{
    PacketWriter w;
    w.reserve(LeaveNotifyPkt::kReserveBytes);
    LeaveNotifyPkt{sid}.write(w);
    return w;
}

inline PacketWriter buildEnterNotify(std::uint64_t sid)
{
    PacketWriter w;
    w.reserve(EnterNotifyPkt::kReserveBytes);
    EnterNotifyPkt{sid}.write(w);
    return w;
}

inline PacketWriter buildTopicMoveAck(std::uint32_t world, std::uint32_t channel)
{
    PacketWriter w;
    w.reserve(TopicMoveAckPkt::kReserveBytes);
    TopicMoveAckPkt{world, channel}.write(w);
    return w;
}
} // namespace hyperapp::protocol