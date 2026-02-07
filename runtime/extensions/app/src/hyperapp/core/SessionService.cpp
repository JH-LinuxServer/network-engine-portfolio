#include <hyperapp/core/SessionService.hpp>
#include <hyperapp/core/OutboundPackets.hpp> // [추가] 필수!

#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/connector/ConnectorManager.hpp>
#include <hypernet/net/SessionManager.hpp>
#include <hypernet/net/WorkerLocal.hpp>
#include <hypernet/core/Logger.hpp>

namespace hyperapp
{

// ---------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------
void SessionService::connectTcp(ConnectTcpOptions opt, ConnectTcpCallback cb) noexcept
{
    const int cw = hypernet::core::ThreadContext::currentWorkerId();

    if (cw != ownerWorkerId_)
    {
        if (!scheduler_)
        {
            if (cb)
            {
                core::ConnectTcpResult res;
                res.ok = false;
                res.err = "connectTcp failed: scheduler not injected";
                cb(std::move(res));
            }
            return;
        }

        (void)scheduler_->postToWorker(ownerWorkerId_, [this, opt = std::move(opt), cb = std::move(cb)]() mutable { this->connectTcp(std::move(opt), std::move(cb)); });
        return;
    }

    auto *sm = hypernet::net::WorkerLocal::sessionManager();
    if (!sm)
    {
        if (cb)
        {
            core::ConnectTcpResult res;
            res.ok = false;
            res.err = "connectTcp failed: SessionManager not found";
            cb(std::move(res));
        }
        return;
    }

    hypernet::connector::DialTcpOptions d{};
    d.host = opt.host;
    d.port = opt.port;

    const ScopeId targetScope = opt.targetScope;
    const TopicId targetTopic = opt.targetTopic;

    sm->connectors().dialTcpSession(d,
                                    [this, targetScope, targetTopic, cb = std::move(cb)](bool ok, hypernet::SessionHandle h, std::string err) mutable
                                    {
                                        core::ConnectTcpResult res;
                                        res.ok = ok;
                                        res.session = h;

                                        if (ok)
                                        {
                                            if (targetScope != 0 || targetTopic != 0)
                                            {
                                                (void)reg_.subscribe(h.id(), targetScope, targetTopic);
                                                (void)reg_.setPrimaryTopic(h.id(), targetScope, targetTopic);
                                            }
                                        }
                                        else
                                        {
                                            res.err = std::move(err);
                                        }

                                        if (cb)
                                            cb(std::move(res));
                                    });
}

// ---------------------------------------------------------------------
// 송신 / 브로드캐스트 (최적화 적용)
// ---------------------------------------------------------------------

// [신규] 로컬 즉시 전송 (Owner Thread 전용)
bool SessionService::sendToLocal_(hypernet::SessionHandle::Id sid, std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept
{
    if (!router_)
        return false;

    const int cw = hypernet::core::ThreadContext::currentWorkerId();
    const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);

    // local-only: 현재 서비스의 owner 워커에서, owner 세션만 송신
    if (cw != ownerWorkerId_ || owner != ownerWorkerId_)
        return false;

    hypernet::SessionHandle h;
    // [중요] SessionRegistry에 추가하신 tryGetHandle을 사용합니다.
    if (!reg_.tryGetHandle(sid, h))
        return false;

    // 정책: packet 기반 send로 통일
    auto payload = outbound::copyPayload(body);
    return router_->send(h, outbound::makePacket(opcode, std::move(payload)));
}

// [수정] 일반 전송 (Local이면 즉시, Remote면 TopicBroadcaster 경유)
bool SessionService::sendTo(hypernet::SessionHandle::Id sid, std::uint16_t opcode, const hypernet::protocol::MessageView &body) noexcept
{
    const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);
    if (owner < 0)
        return false;

    const int cw = hypernet::core::ThreadContext::currentWorkerId();

    // 최적화: owner 워커면 즉시(local) 송신
    if (cw == owner)
        return sendToLocal_(sid, opcode, body);

    // cross-worker: TopicBroadcaster 경유 (TopicBroadcaster::sendTo 수정본 사용)
    return bc_.sendTo(sid, opcode, body);
}

void SessionService::broadcastTopic(ScopeId w, TopicId c, std::uint16_t opcode, const hypernet::protocol::MessageView &body, hypernet::SessionHandle::Id exceptSid) noexcept
{
    bc_.broadcastTopic(w, c, opcode, body, exceptSid);
}

// ---------------------------------------------------------------------
// Close (스레드 안전 로직 적용)
// ---------------------------------------------------------------------

// [신규] 로컬 종료 (Owner Thread 전용)
void SessionService::closeLocal(hypernet::SessionHandle::Id sid, std::string reason, int err) noexcept
{
    const int cw = hypernet::core::ThreadContext::currentWorkerId();
    const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);

    if (cw != ownerWorkerId_ || owner != ownerWorkerId_)
        return;

    closeLocal_(sid, std::move(reason), err);
}

void SessionService::closeLocal_(hypernet::SessionHandle::Id sid, std::string reason, int err) noexcept
{
    // Owner Worker에서만 안전하게 종료 시작
    auto *sm = hypernet::net::WorkerLocal::sessionManager();
    if (!sm)
        return;

    sm->beginClose(sid, reason.c_str(), err);
}

// [수정] 일반 종료
void SessionService::close(hypernet::SessionHandle::Id sid, std::string reason, int err) noexcept
{
    const int owner = hypernet::SessionHandle::ownerWorkerFromId(sid);
    if (owner < 0)
        return;

    if (!scheduler_)
        return;

    const int cw = hypernet::core::ThreadContext::currentWorkerId();

    // owner가 아니면 post
    if (cw != owner)
    {
        (void)scheduler_->postToWorker(owner, [this, sid, reason = std::move(reason), err]() mutable { closeLocal_(sid, std::move(reason), err); });
        return;
    }

    closeLocal_(sid, std::move(reason), err);
}

} // namespace hyperapp