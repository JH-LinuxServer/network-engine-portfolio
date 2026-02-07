#include <hyperapp/core/SessionRegistry.hpp>

#include <algorithm>

namespace hyperapp
{
namespace
{
template <typename IndexMap, typename Key> void eraseFromIndex_(IndexMap &idx, const Key &k, hypernet::SessionHandle::Id sid) noexcept
{
    auto it = idx.find(k);
    if (it == idx.end())
        return;

    it->second.erase(sid);
    if (it->second.empty())
        idx.erase(it);
}
} // namespace

// -----------------------------------------------------------------------------
// SessionStore
// -----------------------------------------------------------------------------
void SessionRegistry::SessionStore::add(hypernet::SessionHandle session, ScopeId w, TopicId c) noexcept
{
    const auto sid = session.id();

    StoredSession rec{};
    rec.handle = session;
    rec.ctx.scope = w;
    rec.ctx.topic = c;
    rec.ctx.state = ConnState::Connected;

    records_[sid] = std::move(rec);
}

void SessionRegistry::SessionStore::remove(SessionId sid) noexcept
{
    records_.erase(sid);
}

SessionRegistry::StoredSession *SessionRegistry::SessionStore::find(SessionId sid) noexcept
{
    auto it = records_.find(sid);
    if (it == records_.end())
        return nullptr;
    return &it->second;
}

const SessionRegistry::StoredSession *SessionRegistry::SessionStore::find(SessionId sid) const noexcept
{
    auto it = records_.find(sid);
    if (it == records_.end())
        return nullptr;
    return &it->second;
}

std::shared_ptr<hypernet::SessionHandle> SessionRegistry::SessionStore::tryGetSession(SessionId sid) noexcept
{
    auto *rec = find(sid);
    if (!rec)
        return {};
    return std::make_shared<hypernet::SessionHandle>(rec->handle);
}

std::shared_ptr<const hypernet::SessionHandle> SessionRegistry::SessionStore::tryGetSession(SessionId sid) const noexcept
{
    auto *rec = find(sid);
    if (!rec)
        return {};
    return std::make_shared<const hypernet::SessionHandle>(rec->handle);
}

std::optional<SessionContext> SessionRegistry::SessionStore::tryGetContext(SessionId sid) const noexcept
{
    auto *rec = find(sid);
    if (!rec)
        return std::nullopt;
    return rec->ctx;
}

// -----------------------------------------------------------------------------
// SubscriptionIndex
// -----------------------------------------------------------------------------
bool SessionRegistry::SubscriptionIndex::subscribe(SessionId sid, ScopeId w, TopicId c) noexcept
{
    TopicKey key{w, c};

    SubscriptionState &st = states_[sid]; // lazy create
    if (st.subscriptions.contains(key))
        return false;

    st.subscriptions.insert(key);
    addToTopic_(sid, key);

    // scope index 관리
    auto &cnt = st.scopeRefCount[w];
    if (cnt == 0)
        addToScope_(sid, w);
    ++cnt;

    return true;
}

bool SessionRegistry::SubscriptionIndex::unsubscribe(SessionId sid, ScopeId w, TopicId c) noexcept
{
    auto it = states_.find(sid);
    if (it == states_.end())
        return false;

    SubscriptionState &st = it->second;

    TopicKey key{w, c};
    auto sit = st.subscriptions.find(key);
    if (sit == st.subscriptions.end())
        return false;

    st.subscriptions.erase(sit);
    removeFromTopic_(sid, key);

    // scope index 관리
    auto scIt = st.scopeRefCount.find(w);
    if (scIt != st.scopeRefCount.end())
    {
        if (scIt->second > 0)
            --scIt->second;

        if (scIt->second == 0)
        {
            st.scopeRefCount.erase(scIt);
            removeFromScope_(sid, w);
        }
    }

    // 모든 구독이 정리된 경우: sid 상태는 비워두지 않고 제거 (저장소와 분리)
    if (st.subscriptions.empty())
    {
        // 방어적으로 scopeIndex도 정리 (정상 경로에서는 이미 비어야 함)
        for (const auto &[sw, cnt] : st.scopeRefCount)
        {
            (void)cnt;
            removeFromScope_(sid, sw);
        }
        st.scopeRefCount.clear();

        states_.erase(it);
    }

    return true;
}

void SessionRegistry::SubscriptionIndex::clearAll(SessionId sid) noexcept
{
    auto it = states_.find(sid);
    if (it == states_.end())
        return;

    SubscriptionState &st = it->second;

    // topicIndex 제거
    for (const auto &key : st.subscriptions)
        removeFromTopic_(sid, key);
    st.subscriptions.clear();

    // scopeIndex 제거
    for (const auto &[w, cnt] : st.scopeRefCount)
    {
        (void)cnt;
        removeFromScope_(sid, w);
    }
    st.scopeRefCount.clear();

    states_.erase(it);
}

bool SessionRegistry::SubscriptionIndex::contains(SessionId sid, const TopicKey &key) const noexcept
{
    auto it = states_.find(sid);
    if (it == states_.end())
        return false;
    return it->second.subscriptions.contains(key);
}

bool SessionRegistry::SubscriptionIndex::empty(SessionId sid) const noexcept
{
    auto it = states_.find(sid);
    if (it == states_.end())
        return true;
    return it->second.subscriptions.empty();
}

const std::unordered_set<SessionRegistry::SessionId> *SessionRegistry::SubscriptionIndex::tryGetScopeMembers(ScopeId w) const noexcept
{
    auto it = scopeIndex_.find(w);
    if (it == scopeIndex_.end())
        return nullptr;
    return &it->second;
}

const std::unordered_set<SessionRegistry::SessionId> *SessionRegistry::SubscriptionIndex::tryGetTopicMembers(const TopicKey &key) const noexcept
{
    auto it = topicIndex_.find(key);
    if (it == topicIndex_.end())
        return nullptr;
    return &it->second;
}

void SessionRegistry::SubscriptionIndex::addToTopic_(SessionId sid, const TopicKey &key) noexcept
{
    topicIndex_[key].insert(sid);
}

void SessionRegistry::SubscriptionIndex::removeFromTopic_(SessionId sid, const TopicKey &key) noexcept
{
    eraseFromIndex_(topicIndex_, key, sid);
}

void SessionRegistry::SubscriptionIndex::addToScope_(SessionId sid, ScopeId w) noexcept
{
    scopeIndex_[w].insert(sid);
}

void SessionRegistry::SubscriptionIndex::removeFromScope_(SessionId sid, ScopeId w) noexcept
{
    eraseFromIndex_(scopeIndex_, w, sid);
}

// -----------------------------------------------------------------------------
// SessionRegistry (public API)
// -----------------------------------------------------------------------------
void SessionRegistry::add(hypernet::SessionHandle session, ScopeId w, TopicId c) noexcept
{
    // [수정] assert -> ensure 패턴 적용
    if (!ensureOwnerThread_())
        return;

    const auto sid = session.id();

    store_.add(session, w, c);

    if (w != 0 && c != 0)
        (void)subscribe(sid, w, c);
}

void SessionRegistry::remove(hypernet::SessionHandle::Id sid) noexcept
{
    if (!ensureOwnerThread_())
        return;

    if (!store_.find(sid))
        return;

    subs_.clearAll(sid);
    store_.remove(sid);
}

std::shared_ptr<hypernet::SessionHandle> SessionRegistry::tryGetSession(hypernet::SessionHandle::Id sid) noexcept
{
    if (!ensureOwnerThread_())
        return {};
    return store_.tryGetSession(sid);
}

std::shared_ptr<const hypernet::SessionHandle> SessionRegistry::tryGetSession(hypernet::SessionHandle::Id sid) const noexcept
{
    if (!ensureOwnerThread_())
        return {};
    return store_.tryGetSession(sid);
}

// [1] Handle 조회 (TopicBroadcaster용)
bool SessionRegistry::tryGetHandle(hypernet::SessionHandle::Id sid, hypernet::SessionHandle &out) const noexcept
{
    if (!ensureOwnerThread_())
        return false;

    const auto *rec = store_.find(sid);
    if (!rec || !rec->handle)
        return false;

    out = rec->handle;
    return true;
}

// [2] Context 조회 (Legacy - 호환성 유지)
std::optional<SessionContext> SessionRegistry::tryGetContext(hypernet::SessionHandle::Id sid) const noexcept
{
    if (!ensureOwnerThread_())
        return std::nullopt;
    return store_.tryGetContext(sid);
}

// [3] Context 조회 (Optimized - 신규 추가)
bool SessionRegistry::tryGetContext(hypernet::SessionHandle::Id sid, SessionContext &out) const noexcept
{
    if (!ensureOwnerThread_())
        return false;

    const auto *rec = store_.find(sid);
    if (!rec)
        return false;

    out = rec->ctx;
    return true;
}

void SessionRegistry::setState(hypernet::SessionHandle::Id sid, ConnState st) noexcept
{
    if (!ensureOwnerThread_())
        return;

    auto *rec = store_.find(sid);
    if (!rec)
        return;

    rec->ctx.state = st;
}

void SessionRegistry::setAuth(hypernet::SessionHandle::Id sid, AccountId aid, PlayerId pid) noexcept
{
    if (!ensureOwnerThread_())
        return;

    auto *rec = store_.find(sid);
    if (!rec)
        return;

    rec->ctx.accountId = aid;
    rec->ctx.playerId = pid;
}

bool SessionRegistry::subscribe(hypernet::SessionHandle::Id sid, ScopeId w, TopicId c) noexcept
{
    if (!ensureOwnerThread_())
        return false;

    if (!store_.find(sid))
        return false;

    return subs_.subscribe(sid, w, c);
}

bool SessionRegistry::unsubscribe(hypernet::SessionHandle::Id sid, ScopeId w, TopicId c) noexcept
{
    if (!ensureOwnerThread_())
        return false;

    auto *rec = store_.find(sid);
    if (!rec)
        return false;

    if (!subs_.unsubscribe(sid, w, c))
        return false;

    if (rec->ctx.scope == w && rec->ctx.topic == c)
    {
        rec->ctx.scope = 0;
        rec->ctx.topic = 0;
    }

    if (subs_.empty(sid))
    {
        rec->ctx.scope = 0;
        rec->ctx.topic = 0;
    }

    return true;
}

bool SessionRegistry::unsubscribeAll(hypernet::SessionHandle::Id sid) noexcept
{
    if (!ensureOwnerThread_())
        return false;

    auto *rec = store_.find(sid);
    if (!rec)
        return false;

    subs_.clearAll(sid);

    rec->ctx.scope = 0;
    rec->ctx.topic = 0;
    return true;
}

bool SessionRegistry::setPrimaryTopic(hypernet::SessionHandle::Id sid, ScopeId w, TopicId c) noexcept
{
    if (!ensureOwnerThread_())
        return false;

    auto *rec = store_.find(sid);
    if (!rec)
        return false;

    if (w == 0 && c == 0)
    {
        rec->ctx.scope = 0;
        rec->ctx.topic = 0;
        return true;
    }

    TopicKey key{w, c};
    if (!subs_.contains(sid, key))
        return false;

    rec->ctx.scope = w;
    rec->ctx.topic = c;
    return true;
}

bool SessionRegistry::moveToTopic(hypernet::SessionHandle::Id sid, ScopeId newW, TopicId newC, ScopeId *oldW, TopicId *oldC) noexcept
{
    if (!ensureOwnerThread_())
        return false;

    auto *rec = store_.find(sid);
    if (!rec)
        return false;

    if (oldW)
        *oldW = rec->ctx.scope;
    if (oldC)
        *oldC = rec->ctx.topic;

    subs_.clearAll(sid);

    rec->ctx.scope = 0;
    rec->ctx.topic = 0;

    if (newW != 0 && newC != 0)
    {
        (void)subscribe(sid, newW, newC);
        (void)setPrimaryTopic(sid, newW, newC);
    }
    return true;
}

std::vector<hypernet::SessionHandle> SessionRegistry::snapshotAll(hypernet::SessionHandle::Id exceptSid) const noexcept
{
    if (!ensureOwnerThread_())
        return {};

    std::vector<hypernet::SessionHandle> out;
    const auto &records = store_.records();
    out.reserve(records.size());
    for (auto &[sid, rec] : records)
    {
        if (sid == exceptSid)
            continue;
        if (rec.handle)
            out.push_back(rec.handle);
    }
    return out;
}

std::vector<hypernet::SessionHandle> SessionRegistry::snapshotScope(ScopeId w, hypernet::SessionHandle::Id exceptSid) const noexcept
{
    if (!ensureOwnerThread_())
        return {};

    std::vector<hypernet::SessionHandle> out;
    auto *members = subs_.tryGetScopeMembers(w);
    if (!members)
        return out;

    out.reserve(members->size());
    for (auto sid : *members)
    {
        if (sid == exceptSid)
            continue;
        auto *rec = store_.find(sid);
        if (rec && rec->handle)
            out.push_back(rec->handle);
    }
    return out;
}

std::vector<hypernet::SessionHandle> SessionRegistry::snapshotTopic(ScopeId w, TopicId c, hypernet::SessionHandle::Id exceptSid) const noexcept
{
    if (!ensureOwnerThread_())
        return {};

    std::vector<hypernet::SessionHandle> out;
    TopicKey key{w, c};
    auto *members = subs_.tryGetTopicMembers(key);
    if (!members)
        return out;

    out.reserve(members->size());
    for (auto sid : *members)
    {
        if (sid == exceptSid)
            continue;
        auto *rec = store_.find(sid);
        if (rec && rec->handle)
            out.push_back(rec->handle);
    }
    return out;
}
} // namespace hyperapp