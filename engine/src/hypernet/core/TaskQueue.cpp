#include <hypernet/core/TaskQueue.hpp>
// SpinLockGuard 정의를 위해 필요하다면 아래 헤더도 확인 (TaskQueue.hpp에 포함되어 있다면 생략 가능)
#include <hypernet/util/SpinLock.hpp>

namespace hypernet::core
{

void TaskQueue::push(Task &&task)
{
    // [중요] 여러 워커가 동시에 push하므로 반드시 잠금이 필요합니다.
    // 스핀락 가드를 생성하여 스코프 내에서 잠금을 유지합니다.
    hypernet::util::SpinLockGuard guard(lock_);

    queue_.push(std::move(task));
}

bool TaskQueue::tryPop(Task &outTask)
{
    // 뮤텍스 대신 스핀락 가드 사용
    hypernet::util::SpinLockGuard guard(lock_);

    if (queue_.empty())
    {
        return false;
    }

    outTask = std::move(queue_.front());
    queue_.pop();
    return true;
}

} // namespace hypernet::core
