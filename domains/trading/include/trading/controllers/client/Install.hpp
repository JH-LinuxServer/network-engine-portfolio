// FILE: ./domains/trading/include/trading/controllers/client/Install.hpp
#pragma once

#include <memory>

namespace hyperapp
{
class AppRuntime;
}
namespace hypernet::protocol
{
class Dispatcher;
}
namespace trading
{
struct IController;
}

namespace trading::feature::client
{
std::shared_ptr<trading::IController> Install(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime, int sessions_per_worker);
} // namespace trading::feature::client
