#include <hypernet/net/WorkerSchedulerFactory.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>

namespace
{
class GlobalWorkerScheduler final : public hypernet::IWorkerScheduler
{
  public:
    explicit GlobalWorkerScheduler(std::vector<hypernet::net::EventLoop *> loops) noexcept
        : loops_(std::move(loops))
    {
    }

    bool postToWorker(int workerId, std::function<void()> task) noexcept override
    {
        if (workerId < 0 || workerId >= static_cast<int>(loops_.size()))
            return false;
        if (!loops_[workerId])
            return false;
        loops_[workerId]->post(std::move(task));
        return true;
    }

    int workerCount() const noexcept override { return static_cast<int>(loops_.size()); }

  private:
    std::vector<hypernet::net::EventLoop *> loops_;
};
} // namespace

namespace hypernet::net
{
std::shared_ptr<hypernet::IWorkerScheduler>
makeGlobalWorkerScheduler(std::vector<hypernet::net::EventLoop *> loops) noexcept
{
    return std::make_shared<::GlobalWorkerScheduler>(std::move(loops));
}
} // namespace hypernet::net
