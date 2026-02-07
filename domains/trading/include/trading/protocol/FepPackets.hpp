#pragma once

#include <trading/protocol/FepOpcodes.hpp>

#include <hyperapp/protocol/PacketReader.hpp>
#include <hyperapp/protocol/PacketWriter.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace trading::protocol
{
// ------------------------------------------------------------
// ----- Basic enums -----
// ------------------------------------------------------------
enum class PeerRole : std::uint8_t
{
    Client = 1,
    Gateway = 2,
    Exchange = 3,
};

enum class HelloResult : std::uint8_t
{
    Ok = 0,
    Reject = 1,
};

// ------------------------------------------------------------
// Handshake: RoleHello
// ------------------------------------------------------------
struct RoleHelloReqPkt
{
    static constexpr std::uint16_t kOpcode = kOpcodeRoleHelloReq;
    static constexpr std::size_t kReserveBytes = 4;

    PeerRole role{PeerRole::Client};

    bool read(hyperapp::protocol::PacketReader &r)
    {
        if (r.remaining() < kReserveBytes)
            return false;

        std::uint8_t temp;
        if (!r.readU8(temp))
            return false;
        role = static_cast<PeerRole>(temp);

        if (!r.skip(3)) // pad
            return false;
        return true;
    }

    void write(hyperapp::protocol::PacketWriter &w) const
    {
        w.writeU8(static_cast<std::uint8_t>(role));
        w.writeU8(0);
        w.writeU16Be(0);
    }
};

struct RoleHelloAckPkt
{
    static constexpr std::uint16_t kOpcode = kOpcodeRoleHelloAck;
    static constexpr std::size_t kReserveBytes = 4;

    HelloResult result{HelloResult::Reject};
    PeerRole role{PeerRole::Client};

    bool read(hyperapp::protocol::PacketReader &r)
    {
        if (r.remaining() < kReserveBytes)
            return false;

        std::uint8_t temp;
        if (!r.readU8(temp))
            return false;
        result = static_cast<HelloResult>(temp);

        if (!r.readU8(temp))
            return false;
        role = static_cast<PeerRole>(temp);

        if (!r.skip(2)) // pad
            return false;
        return true;
    }

    void write(hyperapp::protocol::PacketWriter &w) const
    {
        w.writeU8(static_cast<std::uint8_t>(result));
        w.writeU8(static_cast<std::uint8_t>(role));
        w.writeU16Be(0);
    }
};

// ============================================================================
// PerfPingPkt : Client -> Gateway -> Exchange
// ============================================================================
struct PerfPingPkt
{
    static constexpr std::uint16_t kOpcode = kOpcodePerfPing; // 20050

    std::uint64_t client_sid{0};
    std::uint64_t seq{0};
    std::uint64_t t1{0};
    std::uint64_t t2{0};
    std::uint64_t t3{0};
    std::uint64_t t4{0};

    void write(hyperapp::protocol::PacketWriter &w) const
    {
        w.writeU64Be(client_sid);
        w.writeU64Be(seq);
        w.writeU64Be(t1);
        w.writeU64Be(t2);
        w.writeU64Be(t3);
        w.writeU64Be(t4);
    }

    bool read(hyperapp::protocol::PacketReader &r)
    {
        // [필수] 남은 데이터가 충분한지 확인 (8 bytes * 6 fields = 48 bytes)
        if (r.remaining() < 48)
            return false;

        r.readU64Be(client_sid);
        r.readU64Be(seq);
        r.readU64Be(t1);
        r.readU64Be(t2);
        r.readU64Be(t3);
        r.readU64Be(t4);

        return true;
    }
};

// ============================================================================
// PerfPongPkt : Exchange -> Gateway -> Client
// ============================================================================
struct PerfPongPkt
{
    static constexpr std::uint16_t kOpcode = kOpcodePerfPong; // 20051

    std::uint64_t client_sid{0};
    std::uint64_t seq{0};
    std::uint64_t t1{0};
    std::uint64_t t2{0};
    std::uint64_t t3{0};
    std::uint64_t t4{0};

    void write(hyperapp::protocol::PacketWriter &w) const
    {
        w.writeU64Be(client_sid);
        w.writeU64Be(seq);
        w.writeU64Be(t1);
        w.writeU64Be(t2);
        w.writeU64Be(t3);
        w.writeU64Be(t4);
    }

    bool read(hyperapp::protocol::PacketReader &r)
    {
        // [필수] 남은 데이터가 충분한지 확인
        if (r.remaining() < 48)
            return false;

        r.readU64Be(client_sid);
        r.readU64Be(seq);
        r.readU64Be(t1);
        r.readU64Be(t2);
        r.readU64Be(t3);
        r.readU64Be(t4);
        return true;
    }
};

} // namespace trading::protocol