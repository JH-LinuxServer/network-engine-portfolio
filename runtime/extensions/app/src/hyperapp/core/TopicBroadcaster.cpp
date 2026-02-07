#include <hyperapp/core/TopicBroadcaster.hpp>
#include <hyperapp/core/OutboundPackets.hpp>

#include <utility>
#include <vector>

namespace hyperapp
{
template <typename MakeTargetsFn>
void TopicBroadcaster::broadcastImpl_(MakeTargetsFn &&makeTargets, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid) noexcept
{
    if (!ready_())
        return;

    // 정책: payload deep-copy는 여기서 1회만 수행하고 shared_ptr로 fan-out 공유
    auto payload = outbound::copyPayload(body);

    fanOut_(
        [this, opcode, payload, exceptSid, makeTargets = std::forward<MakeTargetsFn>(makeTargets)](int wid) mutable
        {
            auto *reg = regs_[wid];
            if (!reg)
                return;

            auto targets = makeTargets(*reg);
            if (targets.empty())
                return;

            // 정책: router 호출은 packet 기반으로 통일
            router_->broadcast(targets, outbound::makePacket(opcode, payload));
        });
}

bool TopicBroadcaster::sendTo(hypernet::SessionHandle::Id sid, std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept
{
    if (!ready_())
        return false;

    const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);
    if (owner < 0 || owner >= static_cast<int>(regs_.size()))
        return false;

    // 정책: payload deep-copy는 호출 스레드에서 1회
    auto payload = outbound::copyPayload(body);

    // owner에서만 handle 조회 (cross-thread read 금지)
    return scheduler_->postToWorker(owner,
                                    [this, owner, sid, opcode, payload]() mutable
                                    {
                                        auto *reg = regs_[owner];
                                        if (!reg)
                                            return;

                                        hypernet::SessionHandle h;
                                        if (!reg->tryGetHandle(sid, h))
                                            return;

                                        (void)router_->send(h, outbound::makePacket(opcode, payload));
                                    });
}

void TopicBroadcaster::multicast(const std::vector<hypernet::SessionHandle::Id> &sids, std::uint16_t opcode, const hypernet::protocol::MessageView &body,
                                 hypernet::SessionHandle::Id exceptSid) noexcept
{
    if (!ready_() || sids.empty())
        return;

    const int n = static_cast<int>(regs_.size());
    if (n <= 0)
        return;

    // 정책: payload deep-copy는 호출 스레드에서 1회
    auto payload = outbound::copyPayload(body);

    // owner worker별로 SessionId bucketize
    std::vector<std::vector<hypernet::SessionHandle::Id>> buckets(static_cast<std::size_t>(n));
    for (auto sid : sids)
    {
        if (sid == 0 || sid == exceptSid)
            continue;

        const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);
        if (owner < 0 || owner >= n)
            continue;

        buckets[static_cast<std::size_t>(owner)].push_back(sid);
    }

    const int cw = hypernet::core::wid();

    for (int owner = 0; owner < n; ++owner)
    {
        auto ids = std::move(buckets[static_cast<std::size_t>(owner)]);
        if (ids.empty())
            continue;

        // postToWorker가 std::function 기반이면 callable copy가 발생할 수 있으니,
        // ids는 shared_ptr로 감싸서 copy 비용을 제거한다.
        auto idsPtr = std::make_shared<std::vector<hypernet::SessionHandle::Id>>(std::move(ids));

        auto work = [this, owner, opcode, payload, idsPtr]() mutable
        {
            auto *reg = regs_[owner];
            if (!reg)
                return;

            std::vector<hypernet::SessionHandle> targets;
            targets.reserve(idsPtr->size());

            for (auto sid : *idsPtr)
            {
                hypernet::SessionHandle h;
                if (reg->tryGetHandle(sid, h))
                    targets.push_back(h);
            }

            if (targets.empty())
                return;

            router_->broadcast(targets, outbound::makePacket(opcode, payload));
        };

        if (cw >= 0 && owner == cw)
            work();
        else
            (void)scheduler_->postToWorker(owner, std::move(work));
    }
}

void TopicBroadcaster::broadcastAll(std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid) noexcept
{
    broadcastImpl_([exceptSid](SessionRegistry &reg) { return reg.snapshotAll(exceptSid); }, opcode, body, exceptSid);
}

void TopicBroadcaster::broadcastScope(ScopeId w, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid) noexcept
{
    broadcastImpl_([w, exceptSid](SessionRegistry &reg) { return reg.snapshotScope(w, exceptSid); }, opcode, body, exceptSid);
}

void TopicBroadcaster::broadcastTopic(ScopeId w, TopicId c, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid) noexcept
{
    broadcastImpl_([w, c, exceptSid](SessionRegistry &reg) { return reg.snapshotTopic(w, c, exceptSid); }, opcode, body, exceptSid);
}
} // namespace hyperapp
