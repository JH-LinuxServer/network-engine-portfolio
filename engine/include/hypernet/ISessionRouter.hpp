#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <hypernet/SessionHandle.hpp>
#include <hypernet/protocol/MessageView.hpp>

namespace hypernet
{

/// 다른 스레드로 payload를 넘길 수 있도록 수명을 보장하는 패킷(opcode=U16).
struct RoutedPacketU16
{
    std::uint16_t opcode{0};
    std::shared_ptr<std::vector<std::uint8_t>> body; // nullptr/empty 가능

    [[nodiscard]] protocol::MessageView view() const noexcept
    {
        if (!body || body->empty())
            return protocol::MessageView{nullptr, 0};
        return protocol::MessageView{body->data(), body->size()};
    }

    static RoutedPacketU16 copy(std::uint16_t opcode, const protocol::MessageView &src)
    {
        RoutedPacketU16 p;
        p.opcode = opcode;

        if (src.size() == 0)
        {
            p.body.reset();
            return p;
        }

        auto v = std::make_shared<std::vector<std::uint8_t>>();
        v->resize(src.size());
        if (src.data() != nullptr)
            std::memcpy(v->data(), src.data(), src.size());

        p.body = std::move(v);
        return p;
    }
};

class ISessionRouter
{
  public:
    virtual ~ISessionRouter() = default;

    /// MessageView 기반: cross-thread면 내부에서 copy 수행
    virtual bool send(SessionHandle target, std::uint16_t opcode,
                      const protocol::MessageView &body) noexcept = 0;

    /// 이미 수명 보장된 패킷 기반: cross-thread에서도 copy 없음
    virtual bool send(SessionHandle target, RoutedPacketU16 packet) noexcept = 0;

    /// broadcast: worker별 그룹화 최적화
    virtual void broadcast(const std::vector<SessionHandle> &targets,
                           RoutedPacketU16 packet) noexcept = 0;
};

} // namespace hypernet
