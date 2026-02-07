#pragma once

#include <functional>
#include <memory>

namespace hypernet
{
class IWorkerScheduler
{
  public:
    virtual ~IWorkerScheduler() = default;
    virtual bool postToWorker(int workerId, std::function<void()> task) noexcept = 0;
    virtual int workerCount() const noexcept = 0;
};
} // namespace hypernet