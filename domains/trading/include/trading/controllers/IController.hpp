#pragma once

#include <hyperapp/core/AppRuntime.hpp>
#include <hypernet/SessionHandle.hpp>
#include <hypernet/protocol/Dispatcher.hpp>

namespace trading
{
// 모든 FEP 컨트롤러가 지켜야 할 규약
struct IController
{
    virtual ~IController() = default;

    // Dispatcher에 핸들러를 등록하는 표준 함수
    virtual void install(hypernet::protocol::Dispatcher &dispatcher,
                         hyperapp::AppRuntime &runtime) = 0;

    // ===== App -> Domain lifecycle hook (기본 no-op) =====
    virtual void onServerStart() {}
    virtual void onServerStop() {}

    virtual void onSessionStart(hypernet::SessionHandle /*session*/) {}
    virtual void onSessionEnd(hypernet::SessionHandle /*session*/) {}
};
} // namespace trading