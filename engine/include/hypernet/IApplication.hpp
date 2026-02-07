#pragma once

#include <hypernet/SessionHandle.hpp>

#include <memory>

namespace hypernet::protocol
{
class Dispatcher;
}

namespace hypernet
{
class ISessionRouter;
class IWorkerScheduler;

class IApplication
{
  public:
    virtual ~IApplication() = default;

    virtual void registerHandlers(protocol::Dispatcher &dispatcher) = 0;

    virtual void onServerStart() = 0;
    virtual void onServerStop() = 0;

    virtual void onSessionStart(SessionHandle session) = 0;
    virtual void onSessionEnd(SessionHandle session) = 0;

    // Engine -> App 주입 포인트 (Option A)
    virtual void setSessionRouter(std::shared_ptr<ISessionRouter> router) noexcept { (void)router; }
    virtual void setWorkerScheduler(std::shared_ptr<IWorkerScheduler> s) noexcept { (void)s; }
};
} // namespace hypernet
