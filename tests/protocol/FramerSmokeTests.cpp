#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include <hypernet/buffer/RingBuffer.hpp>
#include <hypernet/protocol/LengthPrefixFramer.hpp>

static std::vector<std::byte> makeFrameBE(const std::vector<std::byte> &payload) {
    const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    std::vector<std::byte> out;
    out.resize(4 + payload.size());
    out[0] = static_cast<std::byte>((len >> 24) & 0xFF);
    out[1] = static_cast<std::byte>((len >> 16) & 0xFF);
    out[2] = static_cast<std::byte>((len >> 8) & 0xFF);
    out[3] = static_cast<std::byte>((len >> 0) & 0xFF);
    for (std::size_t i = 0; i < payload.size(); ++i)
        out[4 + i] = payload[i];
    return out;
}

int main() {
    using hypernet::buffer::RingBuffer;
    using hypernet::protocol::FrameResult;
    using hypernet::protocol::LengthPrefixFramer;
    using hypernet::protocol::MessageView;

    RingBuffer rb(64 * 1024);
    LengthPrefixFramer framer(/*maxPayloadLen=*/1024);

    // 1) 조각난 헤더
    std::vector<std::byte> payload1 = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    auto frame1 = makeFrameBE(payload1);

    rb.write(frame1.data(), 2);
    MessageView mv;
    assert(framer.tryFrame(rb, mv) == FrameResult::NeedMore);

    rb.write(frame1.data() + 2, frame1.size() - 2);
    assert(framer.tryFrame(rb, mv) == FrameResult::Framed);
    assert(mv.size() == payload1.size());

    // 2) 배치(연속 2프레임)
    std::vector<std::byte> payload2 = {std::byte{0x01}, std::byte{0x02}};
    std::vector<std::byte> payload3 = {std::byte{0x10}};
    auto frame2 = makeFrameBE(payload2);
    auto frame3 = makeFrameBE(payload3);

    rb.write(frame2.data(), frame2.size());
    rb.write(frame3.data(), frame3.size());

    assert(framer.tryFrame(rb, mv) == FrameResult::Framed);
    assert(mv.size() == payload2.size());
    assert(framer.tryFrame(rb, mv) == FrameResult::Framed);
    assert(mv.size() == payload3.size());

    // 3) Invalid 길이
    // maxPayloadLen=1024인데 길이를 5000으로 넣기
    std::vector<std::byte> bad(4);
    std::uint32_t badLen = 5000;
    bad[0] = static_cast<std::byte>((badLen >> 24) & 0xFF);
    bad[1] = static_cast<std::byte>((badLen >> 16) & 0xFF);
    bad[2] = static_cast<std::byte>((badLen >> 8) & 0xFF);
    bad[3] = static_cast<std::byte>((badLen >> 0) & 0xFF);

    rb.write(bad.data(), bad.size());
    auto r = framer.tryFrame(rb, mv);
    assert(r == FrameResult::Invalid);

    std::cout << "framer_smoke OK\n";
    return 0;
}
