#pragma once

#include <memory>
#include <vector>

#include <hypernet/IWorkerScheduler.hpp>
#include <hypernet/net/EventLoop.hpp>

namespace hypernet::net
{
std::shared_ptr<hypernet::IWorkerScheduler>
makeGlobalWorkerScheduler(std::vector<hypernet::net::EventLoop *> loops) noexcept;
}