#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <hypernet/protocol/Dispatcher.hpp>

static bool test_constants()
{
    using hypernet::protocol::MessageHeader;

    static_assert(MessageHeader::kLengthFieldBytes == 4);
    static_assert(MessageHeader::kOpcodeFieldBytes == 2);
    static_assert(MessageHeader::kWireBytes == 6);

    return true;
}

static bool test_encode()
{
    using hypernet::protocol::MessageHeader;

    const MessageHeader hdr{
        /*payloadLen=*/7U, // opcode(2) + body(5)
        /*opcode=*/0x1234U,
    };

    std::uint8_t len[MessageHeader::kLengthFieldBytes]{};
    std::uint8_t op[MessageHeader::kOpcodeFieldBytes]{};

    hdr.encodeLen(len);
    hdr.encodeOpcode(op);

    // length = 7 => 00 00 00 07
    assert(len[0] == 0x00);
    assert(len[1] == 0x00);
    assert(len[2] == 0x00);
    assert(len[3] == 0x07);

    // opcode = 0x1234 => 12 34
    assert(op[0] == 0x12);
    assert(op[1] == 0x34);

    return true;
}

int main()
{
    bool ok = true;
    ok = ok && test_constants();
    ok = ok && test_encode();

    if (!ok)
    {
        std::cerr << "MessageHeader tests FAILED\n";
        return 1;
    }
    std::cout << "MessageHeader tests PASSED\n";
    return 0;
}
