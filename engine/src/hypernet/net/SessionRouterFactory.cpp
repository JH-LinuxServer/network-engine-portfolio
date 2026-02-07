#include <hypernet/net/SessionRouterFactory.hpp>

#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>

namespace
{

using namespace hypernet;

class GlobalSessionRouter final : public ISessionRouter
{
  public:
    explicit GlobalSessionRouter(std::vector<net::EventLoop *> loops) noexcept
        : loops_(std::move(loops))
    {
    }

    bool send(SessionHandle target, std::uint16_t opcode,
              const protocol::MessageView &body) noexcept override
    {
        if (!target)
            return false;

        const int owner = target.ownerWorkerId();
        if (owner < 0 || owner >= static_cast<int>(loops_.size()))
            return false;

        // [최적화] 같은 워커라면: 복사 없이 즉시 로컬 송신
        if (core::ThreadContext::currentWorkerId() == owner)
        {
            return target.sendLocalPacketU16(opcode, body);
        }

        // 다른 워커라면: 생명주기 안전을 위해 패킷 복사 후 큐잉 (아래 오버로딩 호출)
        return send(target, RoutedPacketU16::copy(opcode, body));
    }

    bool send(SessionHandle target, RoutedPacketU16 packet) noexcept override
    {
        if (!target)
            return false;

        const int owner = target.ownerWorkerId();
        if (owner < 0 || owner >= static_cast<int>(loops_.size()))
            return false;

        // [최적화] 우연히 현재 스레드가 타겟 워커라면 즉시 전송
        if (core::ThreadContext::currentWorkerId() == owner)
        {
            const auto view = packet.view();
            return target.sendLocalPacketU16(packet.opcode, view);
        }

        auto *loop = loops_[owner];
        if (!loop)
            return false;

        // [Routing] 요청 로그 (자동 태그 적용됨)
        SLOG_INFO("SessionRouter", "PostPacket", "to_w={} sid={} opcode={}", owner, target.id(),
                  packet.opcode);

        loop->post(
            [target, packet]() mutable
            {
                // [Task] 실행 로그
                SLOG_INFO("SessionRouter", "SendPacketTask", "sid={} owner_w={} opcode={}",
                          target.id(), target.ownerWorkerId(), packet.opcode);

                const auto view = packet.view();
                (void)target.sendLocalPacketU16(packet.opcode, view);
            });
        return true; // queued
    }

    void broadcast(const std::vector<SessionHandle> &targets,
                   RoutedPacketU16 packet) noexcept override
    {
        if (targets.empty())
            return;

        const int cw = core::ThreadContext::currentWorkerId();
        const int n = static_cast<int>(loops_.size());

        // 워커별로 세션을 분류 (Grouping)
        std::vector<std::vector<SessionHandle>> groups;
        groups.resize(static_cast<std::size_t>(n));

        for (auto s : targets)
        {
            if (!s)
                continue;
            const int owner = s.ownerWorkerId();
            if (owner < 0 || owner >= n)
                continue;
            groups[static_cast<std::size_t>(owner)].push_back(s);
        }

        for (int owner = 0; owner < n; ++owner)
        {
            auto &g = groups[static_cast<std::size_t>(owner)];
            if (g.empty())
                continue;

            // 1. 내 워커에 속한 세션들: 즉시 전송
            if (cw == owner)
            {
                const auto view = packet.view();
                for (auto &s : g)
                    (void)s.sendLocalPacketU16(packet.opcode, view);
                continue;
            }

            // 2. 다른 워커: 큐잉 (Post)
            auto *loop = loops_[owner];
            if (!loop)
                continue;

            // [Routing] 요청 로그
            SLOG_INFO("SessionRouter", "PostBroadcast", "to_w={} targets={} opcode={}", owner,
                      g.size(), packet.opcode);

            auto moved = std::move(g);
            loop->post(
                [group = std::move(moved), packet]() mutable
                {
                    // [Task] 실행 로그
                    SLOG_INFO("SessionRouter", "BroadcastTask", "targets={} opcode={}",
                              group.size(), packet.opcode);

                    const auto view = packet.view();
                    for (auto &s : group)
                        (void)s.sendLocalPacketU16(packet.opcode, view);
                });
        }
    }

  private:
    std::vector<net::EventLoop *> loops_;
};

} // namespace

namespace hypernet::net
{
std::shared_ptr<ISessionRouter> makeGlobalSessionRouter(std::vector<EventLoop *> loops) noexcept
{
    return std::make_shared<::GlobalSessionRouter>(std::move(loops));
}
} // namespace hypernet::net
