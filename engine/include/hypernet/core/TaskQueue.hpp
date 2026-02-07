#pragma once

#include <functional>
#include <queue> // std::mutex 제거

#include <hypernet/util/NonCopyable.hpp>
#include <hypernet/util/SpinLock.hpp> // 스핀락 헤더 포함

namespace hypernet::core
{

class TaskQueue : private hypernet::util::NonCopyable
{
  public:
    using Task = std::function<void()>;

    TaskQueue() = default;
    ~TaskQueue() = default;

    TaskQueue(TaskQueue &&) = delete;
    TaskQueue &operator=(TaskQueue &&) = delete;

    /// 작업을 큐에 추가합니다. (Thread-Safe: SpinLock 사용)
    void push(Task &&task);

    /// 큐에서 작업을 꺼냅니다. (Thread-Safe: SpinLock 사용)
    bool tryPop(Task &outTask);

  private:
    // std::mutex mutex_; // 뮤텍스 제거
    hypernet::util::SpinLock lock_; // 스핀락 사용
    std::queue<Task> queue_;
};

} // namespace hypernet::core
