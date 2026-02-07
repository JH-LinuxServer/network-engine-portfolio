#include <hyperapp/jobs/JobSystem.hpp>

namespace hyperapp::jobs
{
JobSystem::~JobSystem()
{
    stop();
}

void JobSystem::start(std::size_t threads)
{
    stop();
    stopping_ = false;
    threads_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i)
        threads_.emplace_back([this] { workerLoop_(); });
}

void JobSystem::stop()
{
    {
        std::scoped_lock lk(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto &t : threads_)
        if (t.joinable())
            t.join();
    threads_.clear();

    std::scoped_lock lk(mu_);
    while (!q_.empty())
        q_.pop();
}

void JobSystem::submitForSession(hypernet::SessionHandle::Id sid, std::function<void()> jobWork,
                                 std::function<void(hypernet::SessionHandle::Id)> onDone)
{
    // jobWork는 pool thread에서 실행
    // onDone은 owner worker로 post 되어 실행
    std::scoped_lock lk(mu_);
    q_.push(
        [this, sid, jobWork = std::move(jobWork), onDone = std::move(onDone)]() mutable
        {
            // 1. 무거운 작업(DB, AI 등) 수행 (Job Thread)
            if (jobWork)
                jobWork();

            // 2. 스케줄러가 없으면 복귀 불가 (종료 상황 등)
            if (!scheduler_)
                return;

            // [핵심 변경] 레지스트리 조회 없이 ID 비트 연산으로 주인 즉시 계산
            // -> Job 스레드가 Main/Worker 데이터에 접근할 필요가 아예 사라짐 (Zero Dependency)
            const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);

            // 3. 완료 콜백을 owner worker로 되돌림
            // (만약 그 사이 세션이 끊겼다면? -> Owner Worker가 콜백 실행 시점에 체크함. 안전!)
            (void)scheduler_->postToWorker(owner,
                                           [sid, onDone = std::move(onDone)]() mutable
                                           {
                                               if (onDone)
                                                   onDone(sid);
                                           });
        });
    cv_.notify_one();
}

void JobSystem::workerLoop_()
{
    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [&] { return stopping_ || !q_.empty(); });
            if (stopping_ && q_.empty())
                return;
            task = std::move(q_.front());
            q_.pop();
        }
        if (task)
            task();
    }
}
} // namespace hyperapp::jobs