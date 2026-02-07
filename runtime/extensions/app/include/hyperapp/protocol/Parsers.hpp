#pragma once

#include <hyperapp/protocol/PacketReader.hpp>

#include <cstdint>

namespace hyperapp::protocol
{
// Builders.hpp 규약과 1:1 대응되는 "프레임워크 기본 메시지" 파서들.
// - EnterNotify: U64BE(sid)
// - LeaveNotify: U64BE(sid)
// - TopicMoveAck:     U32BE(world) + U32BE(channel)
// 기본 정책: "정확히 해당 길이만" 와야 성공(expectEnd()).

struct EnterNotify
{
    std::uint64_t sid{0};
};

struct LeaveNotify
{
    std::uint64_t sid{0};
};

struct TopicMoveAck
{
    std::uint32_t world{0};
    std::uint32_t channel{0};
};

inline bool parseEnterNotify(const hypernet::protocol::MessageView &body, EnterNotify &out) noexcept
{
    PacketReader r(body);
    return r.readU64Be(out.sid) && r.expectEnd();
}

inline bool parseLeaveNotify(const hypernet::protocol::MessageView &body, LeaveNotify &out) noexcept
{
    PacketReader r(body);
    return r.readU64Be(out.sid) && r.expectEnd();
}

inline bool parseTopicMoveAck(const hypernet::protocol::MessageView &body, TopicMoveAck &out) noexcept
{
    PacketReader r(body);
    return r.readU32Be(out.world) && r.readU32Be(out.channel) && r.expectEnd();
}
} // namespace hyperapp::protocol