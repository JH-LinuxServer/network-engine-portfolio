#include "MockExchangeApplication.hpp"

// [필수 include] IController 및 Install 팩토리
#include <trading/controllers/IController.hpp>
#include <trading/controllers/exchange/Install.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>

namespace exchange
{

// [수정] 부모 생성자에 기본 워커 수(1) 힌트 전달
MockExchangeApplication::MockExchangeApplication() : fep::apps::WorkerControllerAppBase(1) {}

void MockExchangeApplication::registerHandlers(hypernet::protocol::Dispatcher &dispatcher) noexcept
{
    const int wid = hypernet::core::wid();

    // [수정] Controller 설치 (Install 함수 호출)
    auto controller = trading::feature::exchange::Install(dispatcher, runtime_);

    // [수정] 부모 클래스 API 변경 반영 (setControllerForWorker_ -> setController)
    setController(wid, std::move(controller));
}

void MockExchangeApplication::onServerStart()
{
    if (!scheduler_)
    {
        // [수정] SLOG 규격 준수 (Component, Event, Message)
        SLOG_WARN("MockExchange", "Start", "scheduler not injected");
        return;
    }

    const int n = scheduler_->workerCount();
    // [수정] SLOG 규격 준수
    SLOG_INFO("MockExchange", "Start", "workers={}, status=WaitingForFepInbound", n);

    for (int i = 0; i < n; ++i)
    {
        scheduler_->postToWorker(i,
                                 [this, i]()
                                 {
                                     // [수정] 부모 클래스 API 변경 반영 (controllerForWorker_ -> getController)
                                     if (auto controller = getController(i); controller)
                                         controller->onServerStart();
                                 });
    }
}

} // namespace exchange