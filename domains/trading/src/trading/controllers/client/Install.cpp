// FILE: ./domains/trading/src/trading/controllers/client/Install.cpp
#include <trading/controllers/client/Install.hpp>

#include <trading/controllers/CompositeController.hpp>

#include <trading/feature/benchmark/client/BenchmarkClientController.hpp>
#include <trading/feature/handshake/client/RoleHelloClientController.hpp>

namespace trading::feature::client
{

std::shared_ptr<trading::IController> Install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime, int sessions_per_worker)
{
    auto root = std::make_shared<trading::controllers::CompositeController>();

    auto roleHello = std::make_shared<trading::feature::client::RoleHelloClientController>();
    auto bench = std::make_shared<trading::feature::benchmark::client::BenchmarkClientController>(sessions_per_worker);

    // Handshake OK 되면 bench 쪽에 세션을 넘겨서 "모으고/시작"하게 함
    roleHello->setOnOk([bench](hyperapp::AppRuntime &rt, hypernet::SessionHandle s) { bench->onHandshakeOk(rt, s); });

    root->add(roleHello);
    root->add(bench);

    root->install(dispatcher, runtime);
    return root;
}

} // namespace trading::feature::client
