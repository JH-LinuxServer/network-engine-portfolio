#pragma once

#include <hypernet/IWorkerScheduler.hpp>
#include <hypernet/SessionHandle.hpp>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace hyperapp::jobs
{
class JobSystem
{
  public:
    JobSystem() = default;
    ~JobSystem();

    void start(std::size_t threads);
    void stop();

    void setScheduler(std::shared_ptr<hypernet::IWorkerScheduler> s) noexcept
    {
        scheduler_ = std::move(s);
    }
    void submitForSession(hypernet::SessionHandle::Id sid, std::function<void()> jobWork,
                          std::function<void(hypernet::SessionHandle::Id)> onDone);

  private:
    void workerLoop_();

  private:
    std::shared_ptr<hypernet::IWorkerScheduler> scheduler_;

    std::mutex mu_;
    std::condition_variable cv_;
    bool stopping_{false};

    std::queue<std::function<void()>> q_;
    std::vector<std::thread> threads_;
};
} // namespace hyperapp::jobs