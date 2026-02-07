#pragma once

#include <hyperapp/core/TopicBroadcaster.hpp>
#include <hyperapp/core/SessionService.hpp>
#include <hyperapp/core/SessionRegistry.hpp>
#include <hyperapp/core/SessionStateMachine.hpp>
#include <hyperapp/jobs/JobSystem.hpp>
#include <hyperapp/protocol/PacketReader.hpp>

#include <hypernet/ISessionRouter.hpp>
#include <hypernet/IWorkerScheduler.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/protocol/Dispatcher.hpp>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <type_traits>
#include <functional>

namespace hyperapp
{

class AppRuntime
{
  public:
    AppRuntime();
    ~AppRuntime();

    void setRouter(std::shared_ptr<hypernet::ISessionRouter> r) noexcept;
    void setWorkerScheduler(std::shared_ptr<hypernet::IWorkerScheduler> s) noexcept;

    // [참고] setClientDialer는 엔진 설정 단계에서 IApplication이 처리하므로
    // AppRuntime에서는 굳이 들고 있을 필요가 없습니다. (SessionService가 WorkerLocal로 처리)

    // 세션 생명주기 (Engine -> App -> Runtime)
    void onSessionStart(hypernet::SessionHandle session, ScopeId w, TopicId c);
    void onSessionEnd(hypernet::SessionHandle session);

    // [FIX] connectTcp 제거 (SessionService로 이동됨)

    SessionService &service() noexcept;
    SessionStateMachine &stateMachine() noexcept;
    hyperapp::jobs::JobSystem &jobs() noexcept { return jobs_; }
    [[deprecated("Use runtime.service().broadcast*/multicast/sendTo instead.")]]
    TopicBroadcaster &broadcaster() noexcept
    {
        return broadcaster_;
    }
    // ===== Packet Handler Registration Logic =====
    template <typename PacketType, typename HandlerFn, typename BadFn = std::nullptr_t>
    void registerPacketHandlerCtx(hypernet::protocol::Dispatcher &dispatcher, std::uint16_t opcode, std::uint32_t allowedMask, HandlerFn &&fn, BadFn &&onBadPacket = nullptr, bool strict = true)
    {
        // ... (기존 구현 그대로 유지)
        if (auto *sh = localOrNull_())
        {
            sh->sm.registerPacketHandlerCtx<PacketType>(dispatcher, opcode, allowedMask, std::forward<HandlerFn>(fn), std::forward<BadFn>(onBadPacket), strict);
            return;
        }

        recordDeferredAllowed_(opcode, allowedMask);

        dispatcher.registerHandler(opcode,
                                   [this, opcode, allowedMask, fn = std::forward<HandlerFn>(fn), onBadPacket = std::forward<BadFn>(onBadPacket),
                                    strict](hypernet::SessionHandle s, const hypernet::protocol::MessageView &raw) mutable
                                   {
                                       hyperapp::SessionContext ctx{};
                                       if (!tryGetContext_(s.id(), ctx))
                                           return;

                                       if ((allowedMask & stateBit(ctx.state)) == 0)
                                           return;

                                       hyperapp::protocol::PacketReader r(raw);
                                       PacketType pkt{};

                                       const bool ok = pkt.read(r) && (!strict || r.expectEnd());
                                       if (!ok)
                                       {
                                           if constexpr (!std::is_same_v<std::decay_t<BadFn>, std::nullptr_t>)
                                               onBadPacket(s, raw, ctx);
                                           return;
                                       }

                                       fn(s, pkt, ctx);
                                   });
    }

    template <typename PacketType, typename HandlerFn, typename BadFn = std::nullptr_t>
    void registerPacketHandlerCtx(hypernet::protocol::Dispatcher &dispatcher, std::uint32_t allowedMask, HandlerFn &&fn, BadFn &&onBadPacket = nullptr, bool strict = true)
    {
        registerPacketHandlerCtx<PacketType>(dispatcher, PacketType::kOpcode, allowedMask, std::forward<HandlerFn>(fn), std::forward<BadFn>(onBadPacket), strict);
    }

    bool postToSessionOwner(hypernet::SessionHandle::Id sid, std::function<void()> task) noexcept;
    bool postToSessionOwner(hypernet::SessionHandle session, std::function<void()> task) noexcept;
    bool postToWorker(int wid, std::function<void()> task) noexcept;

  private:
    struct WorkerShard
    {
        int wid;
        SessionRegistry reg;
        SessionStateMachine sm;
        SessionService svc;

        WorkerShard(int w, TopicBroadcaster &bc)
            : wid(w), reg(w), sm(reg), // [수정] sm(w) -> sm(reg) (Registry 참조 전달)
              svc(w, reg, bc)          // [변경] ownerWorkerId 주입
        {
        }
    };
    WorkerShard &local_();
    WorkerShard *localOrNull_() noexcept;
    bool tryGetContext_(hypernet::SessionHandle::Id sid, hyperapp::SessionContext &out) noexcept;
    void recordDeferredAllowed_(std::uint16_t opcode, std::uint32_t mask) noexcept;
    void applyDeferredAllowedStates_() noexcept;

    void rebuildRegistryView_();

  private:
    std::shared_ptr<hypernet::ISessionRouter> router_;
    std::shared_ptr<hypernet::IWorkerScheduler> scheduler_;

    TopicBroadcaster broadcaster_;
    hyperapp::jobs::JobSystem jobs_;

    std::vector<std::unique_ptr<WorkerShard>> shards_;

    std::mutex deferredMu_;
    std::vector<std::pair<std::uint16_t, std::uint32_t>> deferredAllowed_;
};

} // namespace hyperapp