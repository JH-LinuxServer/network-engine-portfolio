#include <trading/controllers/gateway/Install.hpp>

#include <trading/controllers/CompositeController.hpp>
#include <trading/feature/benchmark/gateway/BenchmarkGatewayController.hpp>
#include <trading/feature/handshake/gateway/RoleHelloGatewayController.hpp>

namespace trading::feature::gateway // [변경] controllers -> feature (기존 네임스페이스 유지)
{

std::shared_ptr<IController> Install(hypernet::protocol::Dispatcher &dispatcher,
                                  hyperapp::AppRuntime &runtime,
                                  std::shared_ptr<hyperapp::UpstreamGateway> upstream,
                                  bool handoff_mode)
{
    auto root = std::make_shared<trading::controllers::CompositeController>();

    // [중요] UpstreamGateway 주입 (Phase 1 리팩토링 반영)
    root->add(std::make_shared<trading::feature::gateway::RoleHelloGatewayController>(upstream));
    root->add(std::make_shared<trading::feature::benchmark::gateway::BenchmarkGatewayController>(upstream, handoff_mode));

    root->install(dispatcher, runtime);
    return root;
}

} // namespace trading::feature::gateway

