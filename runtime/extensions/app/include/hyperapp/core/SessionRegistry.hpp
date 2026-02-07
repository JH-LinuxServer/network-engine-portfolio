#pragma once

#include <hyperapp/core/SessionContext.hpp>

#include <hypernet/SessionHandle.hpp>
#include <hypernet/core/ThreadContext.hpp> // [필수] 스레드 ID 확인용

#include <cassert> // [추가] assert용
#include <cstddef>
#include <cstdint>
#include <cstdlib> // [추가] abort용
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hyperapp
{
struct TopicKey
{
    ScopeId scope{0};
    TopicId topic{0};

    bool operator==(const TopicKey &) const noexcept = default;
};

struct TopicKeyHash
{
    std::size_t operator()(const TopicKey &k) const noexcept { return (static_cast<std::size_t>(k.scope) << 32) ^ static_cast<std::size_t>(k.topic); }
};

class SessionRegistry final
{
  public:
    explicit SessionRegistry(int ownerWorkerId) noexcept : ownerWorkerId_(ownerWorkerId) {}

    // 세션 등록/해제
    void add(hypernet::SessionHandle session, ScopeId w, TopicId c) noexcept;
    void remove(hypernet::SessionHandle::Id sid) noexcept;

    // 조회
    std::shared_ptr<hypernet::SessionHandle> tryGetSession(hypernet::SessionHandle::Id sid) noexcept;
    std::shared_ptr<const hypernet::SessionHandle> tryGetSession(hypernet::SessionHandle::Id sid) const noexcept;

    // [1] Handle 조회 (TopicBroadcaster용)
    [[nodiscard]] bool tryGetHandle(hypernet::SessionHandle::Id sid, hypernet::SessionHandle &out) const noexcept;

    // [2] Context 조회 - Legacy (호환성 유지용)
    std::optional<SessionContext> tryGetContext(hypernet::SessionHandle::Id sid) const noexcept;

    // [3] Context 조회 - Optimized (SessionStateMachine용 - 신규 추가)
    [[nodiscard]] bool tryGetContext(hypernet::SessionHandle::Id sid, SessionContext &out) const noexcept;

    // 상태/인증
    void setState(hypernet::SessionHandle::Id sid, ConnState st) noexcept;
    void setAuth(hypernet::SessionHandle::Id sid, AccountId aid, PlayerId pid) noexcept;

    // ---------------------------------------------------------------------
    // Subscription (명시적)
    // ---------------------------------------------------------------------
    [[nodiscard]] bool subscribe(hypernet::SessionHandle::Id sid, ScopeId w, TopicId c) noexcept;
    [[nodiscard]] bool unsubscribe(hypernet::SessionHandle::Id sid, ScopeId w, TopicId c) noexcept;
    [[nodiscard]] bool unsubscribeAll(hypernet::SessionHandle::Id sid) noexcept;

    // Primary 변경
    [[nodiscard]] bool setPrimaryTopic(hypernet::SessionHandle::Id sid, ScopeId w, TopicId c) noexcept;

    // ---------------------------------------------------------------------
    // Snapshot
    // ---------------------------------------------------------------------
    std::vector<hypernet::SessionHandle> snapshotAll(hypernet::SessionHandle::Id exceptSid = 0) const noexcept;
    std::vector<hypernet::SessionHandle> snapshotScope(ScopeId w, hypernet::SessionHandle::Id exceptSid = 0) const noexcept;
    std::vector<hypernet::SessionHandle> snapshotTopic(ScopeId w, TopicId c, hypernet::SessionHandle::Id exceptSid = 0) const noexcept;
    std::vector<hypernet::SessionHandle> snapshotTopic(ScopeId w, TopicId c) const noexcept { return snapshotTopic(w, c, 0); }

    // ---------------------------------------------------------------------
    // Legacy
    // ---------------------------------------------------------------------
    [[deprecated("Use unsubscribeAll+subscribe+setPrimaryTopic explicitly.")]] [[nodiscard]] bool moveToTopic(hypernet::SessionHandle::Id sid, ScopeId newW, TopicId newC, ScopeId *oldW = nullptr,
                                                                                                              TopicId *oldC = nullptr) noexcept;

  private:
    using SessionId = hypernet::SessionHandle::Id;

    struct StoredSession
    {
        hypernet::SessionHandle handle{};
        SessionContext ctx{};
    };

    struct SubscriptionState
    {
        std::unordered_set<TopicKey, TopicKeyHash> subscriptions{};
        std::unordered_map<ScopeId, std::size_t> scopeRefCount{};
    };

    class SessionStore final
    {
      public:
        void add(hypernet::SessionHandle session, ScopeId w, TopicId c) noexcept;
        void remove(SessionId sid) noexcept;

        [[nodiscard]] StoredSession *find(SessionId sid) noexcept;
        [[nodiscard]] const StoredSession *find(SessionId sid) const noexcept;

        [[nodiscard]] std::shared_ptr<hypernet::SessionHandle> tryGetSession(SessionId sid) noexcept;
        [[nodiscard]] std::shared_ptr<const hypernet::SessionHandle> tryGetSession(SessionId sid) const noexcept;
        [[nodiscard]] std::optional<SessionContext> tryGetContext(SessionId sid) const noexcept;
        [[nodiscard]] const std::unordered_map<SessionId, StoredSession> &records() const noexcept { return records_; }

      private:
        std::unordered_map<SessionId, StoredSession> records_{};
    };

    class SubscriptionIndex final
    {
      public:
        [[nodiscard]] bool subscribe(SessionId sid, ScopeId w, TopicId c) noexcept;
        [[nodiscard]] bool unsubscribe(SessionId sid, ScopeId w, TopicId c) noexcept;
        void clearAll(SessionId sid) noexcept;

        [[nodiscard]] bool contains(SessionId sid, const TopicKey &key) const noexcept;
        [[nodiscard]] bool empty(SessionId sid) const noexcept;

        [[nodiscard]] const std::unordered_set<SessionId> *tryGetScopeMembers(ScopeId w) const noexcept;
        [[nodiscard]] const std::unordered_set<SessionId> *tryGetTopicMembers(const TopicKey &key) const noexcept;

      private:
        void addToTopic_(SessionId sid, const TopicKey &key) noexcept;
        void removeFromTopic_(SessionId sid, const TopicKey &key) noexcept;
        void addToScope_(SessionId sid, ScopeId w) noexcept;
        void removeFromScope_(SessionId sid, ScopeId w) noexcept;

      private:
        std::unordered_map<SessionId, SubscriptionState> states_{};
        std::unordered_map<ScopeId, std::unordered_set<SessionId>> scopeIndex_{};
        std::unordered_map<TopicKey, std::unordered_set<SessionId>, TopicKeyHash> topicIndex_{};
    };

    // [수정] 스레드 안전성 검사 (assert -> ensure)
    [[nodiscard]] bool ensureOwnerThread_() const noexcept
    {
        if (ownerWorkerId_ < 0)
            return true;

        const int cw = hypernet::core::ThreadContext::currentWorkerId();
        if (cw == ownerWorkerId_)
            return true;

#ifndef NDEBUG
        assert(false && "SessionRegistry accessed from non-owner worker thread");
#endif

#if defined(FEP_BIND_FAILFAST) && FEP_BIND_FAILFAST
        std::abort();
#endif
        return false;
    }

  private:
    int ownerWorkerId_{0};
    SessionStore store_{};
    SubscriptionIndex subs_{};
};
} // namespace hyperapp