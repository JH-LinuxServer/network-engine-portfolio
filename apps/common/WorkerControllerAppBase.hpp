#pragma once

#include <hyperapp/core/AppRuntime.hpp>
#include <hypernet/IApplication.hpp>
#include <hypernet/IWorkerScheduler.hpp>
#include <trading/controllers/IController.hpp>

#include <vector>
#include <memory>
#include <utility>
#include <cassert>

namespace fep::apps
{

class WorkerControllerAppBase : public hypernet::IApplication
{
  public:
    explicit WorkerControllerAppBase(int expectedWorkerCount = 1)
    {
        if (expectedWorkerCount > 0)
        {
            controllersByWorker_.reserve(static_cast<std::size_t>(expectedWorkerCount));
        }
    }

    // [수정] noexcept 추가 (부모 클래스 IApplication 정의와 일치시켜야 함)
    void setWorkerScheduler(std::shared_ptr<hypernet::IWorkerScheduler> scheduler) noexcept override
    {
        scheduler_ = std::move(scheduler);
        runtime_.setWorkerScheduler(scheduler_);

        if (scheduler_)
        {
            const int n = scheduler_->workerCount();
            controllersByWorker_.assign(static_cast<std::size_t>(n), nullptr);
        }
    }

    // [수정] noexcept 추가
    void setSessionRouter(std::shared_ptr<hypernet::ISessionRouter> router) noexcept override { runtime_.setRouter(std::move(router)); }

    void onServerStop() override
    {
        if (!scheduler_)
            return;

        const int n = scheduler_->workerCount();
        for (int i = 0; i < n; ++i)
        {
            scheduler_->postToWorker(i,
                                     [this, i]()
                                     {
                                         if (auto c = getController(i); c)
                                             c->onServerStop();
                                     });
        }
    }

    void onSessionStart(hypernet::SessionHandle session) override
    {
        runtime_.onSessionStart(session, 0, 0);
        if (auto c = getController(session.ownerWorkerId()); c)
            c->onSessionStart(session);
    }

    void onSessionEnd(hypernet::SessionHandle session) override
    {
        if (auto c = getController(session.ownerWorkerId()); c)
            c->onSessionEnd(session);

        runtime_.onSessionEnd(session);
    }

  protected:
    [[nodiscard]] std::shared_ptr<trading::IController> getController(int wid) const noexcept
    {
        if (wid >= 0 && static_cast<std::size_t>(wid) < controllersByWorker_.size())
            return controllersByWorker_[static_cast<std::size_t>(wid)];
        return nullptr;
    }

    void setController(int wid, std::shared_ptr<trading::IController> controller) noexcept
    {
        if (wid >= 0 && static_cast<std::size_t>(wid) < controllersByWorker_.size())
        {
            controllersByWorker_[static_cast<std::size_t>(wid)] = std::move(controller);
        }
    }

  protected:
    hyperapp::AppRuntime runtime_{};
    std::shared_ptr<hypernet::IWorkerScheduler> scheduler_{};

  private:
    std::vector<std::shared_ptr<trading::IController>> controllersByWorker_{};
};

} // namespace fep::apps
