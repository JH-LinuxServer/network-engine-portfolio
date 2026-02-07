#pragma once

#include <hyperapp/core/AppRuntime.hpp>
#include <hyperapp/core/UpstreamGateway.hpp>
#include <hypernet/protocol/Dispatcher.hpp>

#include <memory>

namespace trading
{
struct IController;

namespace feature::gateway
{
std::shared_ptr<IController> Install(hypernet::protocol::Dispatcher &dispatcher,
                                     hyperapp::AppRuntime &runtime,
                                     std::shared_ptr<hyperapp::UpstreamGateway> upstream,
                                     bool handoff_mode = false);
} // namespace feature::gateway
} // namespace trading

