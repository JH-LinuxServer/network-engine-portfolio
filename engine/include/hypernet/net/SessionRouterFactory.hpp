#pragma once

#include <memory>
#include <vector>

#include <hypernet/ISessionRouter.hpp>
#include <hypernet/net/EventLoop.hpp>

namespace hypernet::net
{
/// Engine이 워커들의 EventLoop 포인터들을 모아서 넘기면,
/// cross-thread send/broadcast를 처리하는 라우터를 만들어준다.
std::shared_ptr<hypernet::ISessionRouter>
makeGlobalSessionRouter(std::vector<hypernet::net::EventLoop *> loops) noexcept;
} // namespace hypernet::net
