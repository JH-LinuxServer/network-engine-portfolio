#include <trading/controllers/exchange/Install.hpp>

#include <trading/controllers/CompositeController.hpp>
#include <trading/feature/benchmark/exchange/BenchmarkExchangeController.hpp>
#include <trading/feature/handshake/exchange/RoleHelloExchangeController.hpp>

namespace trading::feature::exchange // [변경] controllers -> feature (기존 네임스페이스 유지)
{

std::shared_ptr<IController> Install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime)
{
    auto root = std::make_shared<trading::controllers::CompositeController>();

    auto hello = std::make_shared<trading::feature::exchange::RoleHelloExchangeController>();
    auto bench = std::make_shared<trading::feature::benchmark::exchange::BenchmarkExchangeController>();

    // [참고] Exchange는 보통 수동적(Passive)이라 setOnOk 트리거가 없어도 되지만,
    // 필요하다면 Client처럼 여기에 추가 가능. 현재는 단순 등록만으로 충분해 보임.

    root->add(hello);
    root->add(bench);

    root->install(dispatcher, runtime);
    return root;
}

} // namespace trading::feature::exchange