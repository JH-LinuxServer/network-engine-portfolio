#pragma once

#include <hypernet/IApplication.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/net/EventLoop.hpp>
#include <hypernet/util/NonCopyable.hpp>

#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <memory>

namespace hypernet::core
{

/// IApplication 콜백을 "특정 워커 스레드"에서 실행하도록 포워딩하는 디스패처입니다.
///
/// - Engine main thread에서 앱 콜백이 실행되는 일을 구조적으로 차단한다.
/// - 주로 onServerStart/onServerStop 같은 "서버 라이프사이클(cold path)"을
///   지정 워커(기본: worker0)로 포워딩하는 용도.
///
/// 주의(세션 귀속 규약과의 관계):
/// - 세션 콜백(onSessionStart/onSessionEnd/onMessage)은
///   "accept한 워커에 귀속" 규약에 따라 **해당 워커에서 직접 호출**하는 것이 기본이다.
/// - 즉, 세션 핫패스에 이 invoker를 무조건 끼워넣어 단일 워커로 직렬화하는 설계는
///   엔진 기본 규약이 아니다(필요 시 앱이 자체 디스패처로 해결).
class AppCallbackInvoker : private hypernet::util::NonCopyable
{
  public:
    using Callback = std::function<void(IApplication &)>;

    AppCallbackInvoker(std::shared_ptr<IApplication> app, hypernet::net::EventLoop *callbackLoop,
                       unsigned int callbackWorkerId) noexcept
        : app_(std::move(app)), loop_(callbackLoop), callbackWorkerId_(callbackWorkerId)
    {
    }

    void post(const char *name, Callback cb) noexcept
    {
        if (!loop_ || !app_ || !cb)
        {
            SLOG_ERROR("AppCallbackInvoker", "PostIgnored", "reason=InvalidArgs name='{}'",
                       name ? name : "(null)");
            return;
        }

        auto app = app_;
        auto *loop = loop_;
        const unsigned int expectedWid = callbackWorkerId_;

        loop->post(
            [app, cb = std::move(cb), name, expectedWid]() mutable
            {
                const int wid = ThreadContext::currentWorkerId();
                // const long tid = ThreadContext::currentTid(); // Logger handles TID automatically

                if (wid != static_cast<int>(expectedWid))
                {
                    SLOG_FATAL("AppCallbackInvoker", "WrongThread",
                               "name='{}' expected_w={} current_w={}", name ? name : "(null)",
                               expectedWid, wid);
                    std::abort();
                }

                SLOG_INFO("AppCallbackInvoker", "CallbackBegin", "name='{}'",
                          name ? name : "(null)");
                cb(*app);
                SLOG_INFO("AppCallbackInvoker", "CallbackEnd", "name='{}'", name ? name : "(null)");
            });
    }

    bool postAndWait(const char *name, Callback cb)
    {
        if (!loop_ || !app_ || !cb)
        {
            SLOG_ERROR("AppCallbackInvoker", "PostWaitFailed", "reason=InvalidArgs name='{}'",
                       name ? name : "(null)");
            return false;
        }

        // Fast-path:
        // - 같은 callbackLoop(owner thread)에서 postAndWait를 호출하면,
        //   큐에 넣고 fut.get()을 기다리는 순간 데드락이 날 수 있다.
        // - owner thread라면 즉시 실행한다(단, worker id 규약은 동일하게 강제).
        if (loop_->isInOwnerThread())
        {
            const int wid = ThreadContext::currentWorkerId();
            // const long tid = ThreadContext::currentTid();
            const unsigned int expectedWid = callbackWorkerId_;

            if (wid != static_cast<int>(expectedWid))
            {
                SLOG_FATAL("AppCallbackInvoker", "WrongThread",
                           "name='{}' expected_w={} current_w={}", name ? name : "(null)",
                           expectedWid, wid);
                std::abort();
            }

            SLOG_INFO("AppCallbackInvoker", "CallbackBegin", "name='{}'", name ? name : "(null)");
            // 예외는 호출자에게 그대로 전달(동기 호출이므로 자연스러운 동작)
            cb(*app_);
            SLOG_INFO("AppCallbackInvoker", "CallbackEnd", "name='{}'", name ? name : "(null)");
            return true;
        }

        const int callerWid = ThreadContext::currentWorkerId();
        const long callerTid = ThreadContext::currentTid();
        SLOG_DEBUG("AppCallbackInvoker", "PostWaitEnqueue", "name='{}' caller_w={} caller_t={}",
                   name ? name : "(null)", callerWid, callerTid);

        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();

        auto app = app_;
        auto *loop = loop_;
        const unsigned int expectedWid = callbackWorkerId_;

        loop->post(
            [app, cb = std::move(cb), name, expectedWid, done]() mutable
            {
                const int wid = ThreadContext::currentWorkerId();
                // const long tid = ThreadContext::currentTid();

                if (wid != static_cast<int>(expectedWid))
                {
                    SLOG_FATAL("AppCallbackInvoker", "WrongThread",
                               "name='{}' expected_w={} current_w={}", name ? name : "(null)",
                               expectedWid, wid);
                    std::abort();
                }

                try
                {
                    SLOG_INFO("AppCallbackInvoker", "CallbackBegin", "name='{}'",
                              name ? name : "(null)");
                    cb(*app);
                    SLOG_INFO("AppCallbackInvoker", "CallbackEnd", "name='{}'",
                              name ? name : "(null)");
                    done->set_value();
                }
                catch (...)
                {
                    done->set_exception(std::current_exception());
                }
            });

        fut.get();

        SLOG_DEBUG("AppCallbackInvoker", "PostWaitDone", "name='{}' caller_w={} caller_t={}",
                   name ? name : "(null)", callerWid, callerTid);
        return true;
    }

  private:
    std::shared_ptr<IApplication> app_;
    hypernet::net::EventLoop *loop_{nullptr}; // non-owning
    unsigned int callbackWorkerId_{0};
};

} // namespace hypernet::core