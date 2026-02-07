#include <hyperapp/protocol/PacketReader.hpp>
#include <hyperapp/protocol/PacketWriter.hpp>

#include <hypernet/protocol/MessageView.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int g_fail = 0;

#define CHECK(expr)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            ++g_fail;                                                                              \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " :: " #expr << "\n";     \
        }                                                                                          \
    } while (0)

static std::uint16_t loadU16BeFromView(const hypernet::protocol::MessageView &v, std::size_t off)
{
    const auto *p = static_cast<const std::uint8_t *>(v.data());
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[off + 0]) << 8) |
                                      (static_cast<std::uint16_t>(p[off + 1]) << 0));
}

static void test_u16_be_roundtrip()
{
    hyperapp::protocol::PacketWriter w;
    w.writeU16Be(0x0102);

    const auto v = w.view();
    CHECK(v.size() == 2);

    const auto *p = static_cast<const std::uint8_t *>(v.data());
    CHECK(p[0] == 0x01);
    CHECK(p[1] == 0x02);

    hyperapp::protocol::PacketReader r(v);
    std::uint16_t x = 0;
    CHECK(r.readU16Be(x));
    CHECK(x == 0x0102);
    CHECK(r.expectEnd());
}

static void test_u32_be_roundtrip()
{
    hyperapp::protocol::PacketWriter w;
    w.writeU32Be(0x01020304);

    const auto v = w.view();
    CHECK(v.size() == 4);

    const auto *p = static_cast<const std::uint8_t *>(v.data());
    CHECK(p[0] == 0x01);
    CHECK(p[1] == 0x02);
    CHECK(p[2] == 0x03);
    CHECK(p[3] == 0x04);

    hyperapp::protocol::PacketReader r(v);
    std::uint32_t x = 0;
    CHECK(r.readU32Be(x));
    CHECK(x == 0x01020304);
    CHECK(r.expectEnd());
}

static void test_u64_be_roundtrip()
{
    constexpr std::uint64_t k = 0x0102030405060708ULL;

    hyperapp::protocol::PacketWriter w;
    w.writeU64Be(k);

    const auto v = w.view();
    CHECK(v.size() == 8);

    const auto *p = static_cast<const std::uint8_t *>(v.data());
    CHECK(p[0] == 0x01);
    CHECK(p[1] == 0x02);
    CHECK(p[2] == 0x03);
    CHECK(p[3] == 0x04);
    CHECK(p[4] == 0x05);
    CHECK(p[5] == 0x06);
    CHECK(p[6] == 0x07);
    CHECK(p[7] == 0x08);

    hyperapp::protocol::PacketReader r(v);
    std::uint64_t x = 0;
    CHECK(r.readU64Be(x));
    CHECK(x == k);
    CHECK(r.expectEnd());
}

static void test_string_u16_roundtrip()
{
    hyperapp::protocol::PacketWriter w;
    w.writeStringU16("ABC");

    const auto v = w.view();
    CHECK(v.size() == 2 + 3);

    // length prefix 확인
    CHECK(loadU16BeFromView(v, 0) == 3);

    hyperapp::protocol::PacketReader r(v);
    std::string_view s;
    CHECK(r.readStringU16(s));
    CHECK(s == "ABC");
    CHECK(r.expectEnd());
}

static void test_expect_end_catches_leftover()
{
    hyperapp::protocol::PacketWriter w;
    w.writeU16Be(0xBEEF);
    w.writeU8(0xFF); // leftover 1 byte

    hyperapp::protocol::PacketReader r(w.view());

    std::uint16_t x = 0;
    CHECK(r.readU16Be(x));
    CHECK(x == 0xBEEF);

    // strict 정책의 핵심: 남는 바이트가 있으면 false
    CHECK(!r.expectEnd());

    // 남은 바이트를 소비하면 end
    CHECK(r.skip(1));
    CHECK(r.expectEnd());
}

static void test_writeStringU16_truncates_over_0xFFFF()
{
    std::string big(70000, 'x'); // 0xFFFF(65535) 초과

    hyperapp::protocol::PacketWriter w;
    w.writeStringU16(big);

    const auto v = w.view();
    CHECK(loadU16BeFromView(v, 0) == 0xFFFF);
    CHECK(v.size() == 2 + 0xFFFF);
}

static void test_writeStringU16Checked_fail_is_noop()
{
    std::string big(70000, 'x');

    hyperapp::protocol::PacketWriter w;
    w.writeU8(0xAB);
    const std::size_t before = w.size();

    const bool ok = w.writeStringU16Checked(big, /*maxLen=*/100);
    CHECK(!ok);
    CHECK(w.size() == before); // 실패 시 버퍼 무변경(= early return)
}

static void test_writeStringU16Checked_success()
{
    hyperapp::protocol::PacketWriter w;
    const bool ok = w.writeStringU16Checked("HELLO", /*maxLen=*/10);
    CHECK(ok);

    hyperapp::protocol::PacketReader r(w.view());
    std::string_view s;
    CHECK(r.readStringU16(s));
    CHECK(s == "HELLO");
    CHECK(r.expectEnd());
}
} // namespace

int main()
{
    test_u16_be_roundtrip();
    test_u32_be_roundtrip();
    test_u64_be_roundtrip();
    test_string_u16_roundtrip();
    test_expect_end_catches_leftover();
    test_writeStringU16_truncates_over_0xFFFF();
    test_writeStringU16Checked_fail_is_noop();
    test_writeStringU16Checked_success();

    if (g_fail == 0)
    {
        std::cout << "[OK] hyperapp.packet_codec (PacketReader/PacketWriter)\n";
        return 0;
    }

    std::cerr << "[NG] failures=" << g_fail << "\n";
    return 1;
}