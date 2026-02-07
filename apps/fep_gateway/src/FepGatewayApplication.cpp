#include "FepGatewayApplication.hpp"

#include <trading/controllers/IController.hpp>
#include <trading/controllers/gateway/Install.hpp>

#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/core/Logger.hpp>

#include <hyperapp/core/SessionService.hpp>

#include <cassert>

namespace fep
{

FepGatewayApplication::FepGatewayApplication(const hypernet::core::FepConfig &cfg)
    : apps::WorkerControllerAppBase(cfg.worker_threads), cfg_(cfg), upstream_(std::make_shared<hyperapp::UpstreamGateway>(cfg.worker_threads))
{
}

// [수정] 헤더와 일치하게 noexcept 추가
void FepGatewayApplication::setWorkerScheduler(std::shared_ptr<hypernet::IWorkerScheduler> scheduler) noexcept
{
    // 1. 부모 클래스 로직 실행
    apps::WorkerControllerAppBase::setWorkerScheduler(scheduler);

    // 2. 추가 로직: UpstreamGateway 리사이징
    if (upstream_ && scheduler)
    {
        upstream_->reset(scheduler->workerCount());
    }
}

void FepGatewayApplication::onSessionEnd(hypernet::SessionHandle session)
{
    apps::WorkerControllerAppBase::onSessionEnd(session);

    if (upstream_ && upstream_->workerCount() > 0 && upstream_->isLocal(session.id()))
    {
        upstream_->clearLocal();
    }
}

void FepGatewayApplication::onServerStart()
{
    assert(scheduler_ != nullptr);

    const int n = scheduler_->workerCount();
    SLOG_INFO("FepGateway", "Start", "started. workers={}", n);

    for (int i = 0; i < n; ++i)
    {
        scheduler_->postToWorker(i,
                                 [this, i]()
                                 {
                                     hyperapp::ConnectTcpOptions opt{};
                                     opt.host = cfg_.upstream_host;
                                     opt.port = cfg_.upstream_port;
                                     opt.targetScope = 0;
                                     opt.targetTopic = 0;

                                     SLOG_DEBUG("FepGateway", "Connect", "worker={} connectTcp {}:{}", i, opt.host, opt.port);
                                     runtime_.service().connectTcp(opt,
                                                                   [this, i](hyperapp::core::ConnectTcpResult res)
                                                                   {
                                                                       if (!res.ok)
                                                                       {
                                                                           SLOG_ERROR("FepGateway", "Connect", "worker={} failed: {}", i, res.err.value_or("unknown"));
                                                                           return;
                                                                       }

                                                                       const auto session = res.session;
                                                                       SLOG_INFO("FepGateway", "Connect", "worker={} upstream connected sid={}", i, session.id());

                                                                       if (upstream_)
                                                                           upstream_->setForWorker(i, session.id());
                                                                   });
                                 });
    }
}

void FepGatewayApplication::registerHandlers(hypernet::protocol::Dispatcher &dispatcher)
{
    const int wid = hypernet::core::wid();
    auto controller = trading::feature::gateway::Install(dispatcher, runtime_, upstream_, cfg_.handoff_mode);
    setController(wid, std::move(controller));
}

} // namespace fep

