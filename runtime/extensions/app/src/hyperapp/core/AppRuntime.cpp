#include <hyperapp/core/AppRuntime.hpp>
#include <hypernet/core/Logger.hpp>        // 로깅용
#include <hypernet/core/ThreadContext.hpp> // [추가] currentWorkerId() 사용을 위해 명시적 포함

namespace hyperapp
{

AppRuntime::AppRuntime() : broadcaster_{}, jobs_{} {}

AppRuntime::~AppRuntime() {}

void AppRuntime::setRouter(std::shared_ptr<hypernet::ISessionRouter> r) noexcept
{
    router_ = std::move(r);
    // broadcaster에도 라우터 주입
    broadcaster_.setRouter(router_);

    // 각 shard 서비스에도 주입
    for (auto &p : shards_)
    {
        if (p)
            p->svc.setRouter(router_);
    }
}

void AppRuntime::setWorkerScheduler(std::shared_ptr<hypernet::IWorkerScheduler> s) noexcept
{
    scheduler_ = std::move(s);
    if (!scheduler_)
        return;

    broadcaster_.setScheduler(scheduler_);
    jobs_.setScheduler(scheduler_);

    if (shards_.empty() && scheduler_)
    {
        const int n = scheduler_->workerCount();
        shards_.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            shards_.emplace_back(std::make_unique<WorkerShard>(i, broadcaster_));
        }

        // router가 이미 주입되어 있으면 서비스에도 반영
        for (auto &p : shards_)
            if (p)
                p->svc.setRouter(router_);

        // [추가] scheduler도 shard 서비스에 주입 (요구사항 2)
        for (auto &p : shards_)
            if (p)
                p->svc.setScheduler(scheduler_);

        rebuildRegistryView_();
        applyDeferredAllowedStates_();
        return;
    }

    // shards_가 이미 존재하는 경우에도 scheduler 재주입 보장
    for (auto &p : shards_)
        if (p)
            p->svc.setScheduler(scheduler_);
}

void AppRuntime::rebuildRegistryView_()
{
    std::vector<SessionRegistry *> regs;
    regs.reserve(shards_.size());
    for (auto &p : shards_)
        regs.push_back(p ? &p->reg : nullptr);

    broadcaster_.setRegistries(std::move(regs));
}

AppRuntime::WorkerShard &AppRuntime::local_()
{
    const int w = hypernet::core::ThreadContext::currentWorkerId(); // [변경] 명시적 함수 사용
    if (w < 0 || w >= static_cast<int>(shards_.size()) || !shards_[w])
    {
        // worker thread가 아닌 곳에서 서비스 접근 시도 시 abort
        std::abort();
    }
    return *shards_[w];
}

SessionService &AppRuntime::service() noexcept
{
    return local_().svc;
}

SessionStateMachine &AppRuntime::stateMachine() noexcept
{
    return local_().sm;
}

// [수정됨] 최적화 적용: 현재 워커가 Owner라면 큐에 넣지 않고 즉시 실행 (Inline)
bool AppRuntime::postToSessionOwner(hypernet::SessionHandle::Id sid, std::function<void()> task) noexcept
{
    if (!scheduler_)
        return false;

    const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);
    const int cw = hypernet::core::ThreadContext::currentWorkerId();

    // 최적화: 이미 목표 워커 스레드라면 즉시 실행하여 오버헤드 제거
    if (cw == owner)
    {
        task();
        return true;
    }

    return scheduler_->postToWorker(owner, std::move(task));
}

// [수정됨] 최적화 적용: 위와 동일 (Overload)
bool AppRuntime::postToSessionOwner(hypernet::SessionHandle session, std::function<void()> task) noexcept
{
    if (!scheduler_)
        return false;

    const int owner = session.ownerWorkerId();
    const int cw = hypernet::core::ThreadContext::currentWorkerId();

    if (cw == owner)
    {
        task();
        return true;
    }

    return scheduler_->postToWorker(owner, std::move(task));
}

bool AppRuntime::postToWorker(int wid, std::function<void()> task) noexcept
{
    if (!scheduler_)
        return false;
    return scheduler_->postToWorker(wid, std::move(task));
}

void AppRuntime::onSessionStart(hypernet::SessionHandle session, ScopeId w, TopicId c)
{
    if (shards_.empty())
        return;

    const int owner = session.ownerWorkerId();
    const int cw = hypernet::core::ThreadContext::currentWorkerId();

    // 이미 owner 워커 스레드면 즉시 등록 (순서 보장)
    if (cw == owner)
    {
        auto &sh = *shards_[owner];
        sh.reg.add(session, w, c);
        return;
    }

    // owner가 아니면 post
    if (!scheduler_)
        return;

    (void)scheduler_->postToWorker(owner,
                                   [this, session, w, c]
                                   {
                                       auto &sh = *shards_[session.ownerWorkerId()];
                                       sh.reg.add(session, w, c);
                                   });
}

void AppRuntime::onSessionEnd(hypernet::SessionHandle session)
{
    if (shards_.empty())
        return;

    const int owner = session.ownerWorkerId();
    const int cw = hypernet::core::ThreadContext::currentWorkerId();
    const auto sid = session.id();

    // owner 워커면 즉시 제거
    if (cw == owner)
    {
        auto &sh = *shards_[owner];
        sh.reg.remove(sid);
        return;
    }

    // owner가 아니면 post
    if (!scheduler_)
        return;

    (void)scheduler_->postToWorker(owner,
                                   [this, owner, sid]
                                   {
                                       auto &sh = *shards_[owner];
                                       sh.reg.remove(sid);
                                   });
}

AppRuntime::WorkerShard *AppRuntime::localOrNull_() noexcept
{
    const int w = hypernet::core::ThreadContext::currentWorkerId();
    if (w < 0 || w >= static_cast<int>(shards_.size()))
        return nullptr;
    return shards_[w] ? shards_[w].get() : nullptr;
}

bool AppRuntime::tryGetContext_(hypernet::SessionHandle::Id sid, hyperapp::SessionContext &out) noexcept
{
    auto *sh = localOrNull_();
    if (!sh)
        return false;

    auto opt = sh->reg.tryGetContext(sid);
    if (opt)
    {
        out = *opt;
        return true;
    }
    return false;
}

void AppRuntime::recordDeferredAllowed_(std::uint16_t opcode, std::uint32_t mask) noexcept
{
    std::lock_guard<std::mutex> lk(deferredMu_);
    deferredAllowed_.emplace_back(opcode, mask);
}

void AppRuntime::applyDeferredAllowedStates_() noexcept
{
    std::vector<std::pair<std::uint16_t, std::uint32_t>> copy;
    {
        std::lock_guard<std::mutex> lk(deferredMu_);
        copy = deferredAllowed_;
    }

    if (copy.empty() || shards_.empty())
        return;

    for (auto &p : shards_)
    {
        if (!p)
            continue;
        for (auto [op, mask] : copy)
            p->sm.setAllowedStates(op, mask);
    }
}

} // namespace hyperapp