#include "LoadgenApplication.hpp"

#include <trading/controllers/IController.hpp>
#include <trading/controllers/client/Install.hpp>
#include <hyperapp/core/SessionService.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>

namespace client
{

// [수정] cfg.worker_threads가 존재하지 않으므로 기본값 1 전달
LoadgenApplication::LoadgenApplication(const hypernet::core::ExchangeSimConfig &cfg) : fep::apps::WorkerControllerAppBase(1), cfg_(cfg) {}

void LoadgenApplication::registerHandlers(hypernet::protocol::Dispatcher &dispatcher)
{
    const int wid = hypernet::core::wid();

    // cfg_.connection_count를 Install로 주입해서 bench가 목표 세션 수를 알게 함
    auto controller = trading::feature::client::Install(dispatcher, runtime_, cfg_.connection_count);

    setController(wid, std::move(controller));
}

void LoadgenApplication::onServerStart()
{
    const int sessions_per_worker = cfg_.connection_count;

    if (!scheduler_)
    {
        SLOG_WARN("Loadgen", "Start", "scheduler not injected");
        return;
    }

    const int n = scheduler_->workerCount();
    SLOG_INFO("Loadgen", "Start", "started. workers={}, fep_host={}, fep_port={}, sessions_per_worker={}", n, cfg_.fep_host, cfg_.fep_port, sessions_per_worker);

    for (int i = 0; i < n; ++i)
    {
        // sessions_per_worker를 람다에 캡처
        scheduler_->postToWorker(i,
                                 [this, i, sessions_per_worker]()
                                 {
                                     if (auto controller = getController(i); controller)
                                         controller->onServerStart();

                                     hyperapp::ConnectTcpOptions opt{};
                                     opt.host = cfg_.fep_host;
                                     opt.port = static_cast<std::uint16_t>(cfg_.fep_port);
                                     opt.targetScope = 0;
                                     opt.targetTopic = 0;

                                     // ✅ 워커당 N개 연결 생성
                                     for (int k = 0; k < sessions_per_worker; ++k)
                                     {
                                         runtime_.service().connectTcp(opt,
                                                                       [i, k](hyperapp::core::ConnectTcpResult res)
                                                                       {
                                                                           if (!res.ok)
                                                                           {
                                                                               SLOG_WARN("Loadgen", "Connect", "worker={} idx={} failed: {}", i, k, res.err.value_or("unknown"));
                                                                               return;
                                                                           }

                                                                           // 너무 로그가 많으면 INFO -> DEBUG로 낮추거나, k==0일 때만 찍어도 됨
                                                                           SLOG_INFO("Loadgen", "Connect", "worker={} idx={} success: sid={}", i, k, res.session.id());
                                                                       });
                                     }
                                 });
    }
}
} // namespace client