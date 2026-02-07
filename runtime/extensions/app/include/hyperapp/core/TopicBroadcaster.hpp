#pragma once

#include <hyperapp/core/SessionRegistry.hpp>

#include <hypernet/ISessionRouter.hpp>
#include <hypernet/IWorkerScheduler.hpp>
#include <hypernet/SessionHandle.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/protocol/MessageView.hpp>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace hyperapp
{
class TopicBroadcaster
{
  public:
    TopicBroadcaster() = default;

    void setRouter(std::shared_ptr<hypernet::ISessionRouter> r) noexcept { router_ = std::move(r); }
    void setScheduler(std::shared_ptr<hypernet::IWorkerScheduler> s) noexcept { scheduler_ = std::move(s); }

    // AppRuntime가 per-worker registry 주소를 주입
    void setRegistries(std::vector<SessionRegistry *> regs) noexcept { regs_ = std::move(regs); }

    // sid만으로도 보낼 수 있게: sid로 owner 계산 → owner에서 handle 조회
    bool sendTo(hypernet::SessionHandle::Id sid, std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept;

    // [NEW] 임의 sid 리스트 멀티캐스트 (엔진이 bucketize + thread-hop + handle 확보 + fan-out 전담)
    void multicast(const std::vector<hypernet::SessionHandle::Id> &sids, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid = 0) noexcept;

    void broadcastAll(std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid = 0) noexcept;

    void broadcastScope(ScopeId w, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid = 0) noexcept;

    void broadcastTopic(ScopeId w, TopicId c, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid = 0) noexcept;

  private:
    [[nodiscard]] bool ready_() const noexcept { return router_ && scheduler_ && !regs_.empty(); }

    template <typename MakeTargetsFn>
    void broadcastImpl_(MakeTargetsFn &&makeTargets, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid) noexcept;

    // worker 전체에 대해 실행 (현재 worker는 즉시 실행, 나머지는 post)
    template <typename Fn> void fanOut_(Fn &&fn) noexcept
    {
        if (!scheduler_ || regs_.empty())
            return;

        using FnT = std::decay_t<Fn>;
        auto sharedFn = std::make_shared<FnT>(std::forward<Fn>(fn));

        const int n = static_cast<int>(regs_.size());
        const int cw = hypernet::core::wid();

        for (int wid = 0; wid < n; ++wid)
        {
            if (cw >= 0 && wid == cw)
            {
                (*sharedFn)(wid);
            }
            else
            {
                (void)scheduler_->postToWorker(wid, [sharedFn, wid]() mutable { (*sharedFn)(wid); });
            }
        }
    }

  private:
    std::shared_ptr<hypernet::ISessionRouter> router_;
    std::shared_ptr<hypernet::IWorkerScheduler> scheduler_;
    std::vector<SessionRegistry *> regs_; // index = workerId
};
} // namespace hyperapp
