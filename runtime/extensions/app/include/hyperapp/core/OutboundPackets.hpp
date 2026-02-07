#pragma once

#include <hypernet/ISessionRouter.hpp> // hypernet::RoutedPacketU16
#include <hypernet/protocol/MessageView.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace hyperapp::outbound
{
using Payload = std::shared_ptr<std::vector<std::uint8_t>>;

/// MessageView -> (수명 보장) payload deep-copy
/// - size==0 이면 nullptr payload (엔진 RoutedPacketU16::copy와 동일한 의미)
/// - data()==nullptr 이면 memcpy 생략(0으로 채워진 버퍼 유지)
[[nodiscard]] inline Payload copyPayload(const hypernet::protocol::MessageView &body) noexcept
{
    if (body.size() == 0)
        return Payload{};

    auto buf = std::make_shared<std::vector<std::uint8_t>>();
    buf->resize(body.size());

    if (body.data() != nullptr)
        std::memcpy(buf->data(), body.data(), body.size());

    return buf;
}

/// opcode + payload -> RoutedPacketU16
[[nodiscard]] inline hypernet::RoutedPacketU16 makePacket(std::uint16_t opcode,
                                                          Payload payload) noexcept
{
    hypernet::RoutedPacketU16 p;
    p.opcode = opcode;
    p.body = std::move(payload);
    return p;
}

/// opcode + body(MessageView) -> RoutedPacketU16 (payload deep-copy 포함)
[[nodiscard]] inline hypernet::RoutedPacketU16
makePacket(std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept
{
    return makePacket(opcode, copyPayload(body));
}

} // namespace hyperapp::outbound
