#include <hypernet/protocol/MessageCodec.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

using hypernet::protocol::ByteReader;
using hypernet::protocol::ByteWriter;

// 예제 메시지 1: 32-bit + 16-bit
struct HelloMsg {
    std::uint32_t magic{0};
    std::uint16_t version{0};

    bool operator==(const HelloMsg &o) const noexcept {
        return magic == o.magic && version == o.version;
    }
};

// 예제 메시지 2: 64-bit
struct PingMsg {
    std::uint64_t nonce{0};

    bool operator==(const PingMsg &o) const noexcept { return nonce == o.nonce; }
};

std::vector<std::byte> encodeHello(const HelloMsg &m) {
    ByteWriter w;
    w.writeU32Be(m.magic);
    w.writeU16Be(m.version);
    return w.release();
}

bool decodeHello(std::span<const std::byte> bytes, HelloMsg &out) {
    ByteReader r(bytes);
    if (!r.readU32Be(out.magic)) {
        return false;
    }
    if (!r.readU16Be(out.version)) {
        return false;
    }
    // “정확히 소비”를 규약으로 고정(남는 바이트가 있으면 다른 메시지와 경계가 흐려짐)
    return r.atEnd();
}

std::vector<std::byte> encodePing(const PingMsg &m) {
    ByteWriter w;
    w.writeU64Be(m.nonce);
    return w.release();
}

bool decodePing(std::span<const std::byte> bytes, PingMsg &out) {
    ByteReader r(bytes);
    if (!r.readU64Be(out.nonce)) {
        return false;
    }
    return r.atEnd();
}

std::uint8_t u8(std::byte b) { return std::to_integer<std::uint8_t>(b); }

} // namespace

int main() {
    // ===== Round-trip 1) HelloMsg =====
    {
        HelloMsg in{};
        in.magic = 0x11223344u;
        in.version = 0x5566u;

        const auto bytes = encodeHello(in);

        // 엔디안 고정 검증(빅엔디안)
        // expected: 11 22 33 44 55 66
        assert(bytes.size() == 6);
        assert(u8(bytes[0]) == 0x11);
        assert(u8(bytes[1]) == 0x22);
        assert(u8(bytes[2]) == 0x33);
        assert(u8(bytes[3]) == 0x44);
        assert(u8(bytes[4]) == 0x55);
        assert(u8(bytes[5]) == 0x66);

        HelloMsg out{};
        const bool ok = decodeHello(bytes, out);
        assert(ok);
        assert(in == out);
    }

    // ===== Round-trip 2) PingMsg =====
    {
        PingMsg in{};
        in.nonce = 0x0102030405060708ull;

        const auto bytes = encodePing(in);

        // expected: 01 02 03 04 05 06 07 08
        assert(bytes.size() == 8);
        for (int i = 0; i < 8; ++i) {
            assert(u8(bytes[static_cast<std::size_t>(i)]) == static_cast<std::uint8_t>(i + 1));
        }

        PingMsg out{};
        const bool ok = decodePing(bytes, out);
        assert(ok);
        assert(in == out);
    }

    // ===== Negative) Truncated decode must fail =====
    {
        HelloMsg in{};
        in.magic = 0xAABBCCDDu;
        in.version = 0xEEFFu;

        auto bytes = encodeHello(in);
        assert(bytes.size() == 6);

        // 1바이트 제거 -> decode 실패
        bytes.pop_back();

        HelloMsg out{};
        const bool ok = decodeHello(bytes, out);
        assert(!ok);
    }

    std::cout << "[OK] MessageCodecTests passed\n";
    return 0;
}
