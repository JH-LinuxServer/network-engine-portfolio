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

namespace trading::feature::exchange
{
std::shared_ptr<trading::IController> Install(hypernet::protocol::Dispatcher &dispatcher,
                                              hyperapp::AppRuntime &runtime);
} // namespace trading::feature::exchange