#pragma once

#include <cstdint>
#include <hypernet/protocol/Endian.hpp>
#include <hypernet/protocol/MessageView.hpp>

namespace hypernet::protocol
{
inline bool splitOpcodeU16Be(const MessageView &msg, std::uint16_t &opcodeOut,
                             MessageView &bodyOut) noexcept
{
    if (msg.size() < 2 || msg.data() == nullptr)
        return false;

    const auto *p = static_cast<const std::uint8_t *>(msg.data());
    opcodeOut = loadU16Be(p);

    const std::size_t bodyLen = msg.size() - 2;
    bodyOut = MessageView{bodyLen ? (p + 2) : nullptr, bodyLen};
    return true;
}
} // namespace hypernet::protocol