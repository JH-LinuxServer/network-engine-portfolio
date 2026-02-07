#pragma once

#include <hypernet/SessionHandle.hpp>
#include <hypernet/core/ThreadContext.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory> // [추가] unique_ptr 사용
#include <vector>

namespace hyperapp
{
class UpstreamGateway final
{
  public:
    using SessionId = hypernet::SessionHandle::Id;

    UpstreamGateway() = default;
    explicit UpstreamGateway(int workerCount) { reset(workerCount); }

    void reset(int workerCount)
    {
        assert(workerCount > 0);

        // [수정] unique_ptr 벡터로 변경
        // atomic 객체 이동 문제를 피하기 위해 포인터로 관리합니다.
        slots_.clear();
        slots_.reserve(static_cast<std::size_t>(workerCount));

        for (int i = 0; i < workerCount; ++i)
        {
            slots_.push_back(std::make_unique<Slot>());
            // 초기화
            slots_.back()->sid.store(0, std::memory_order_relaxed);
        }

        workerCount_ = workerCount;
    }

    [[nodiscard]] int workerCount() const noexcept { return workerCount_; }

    void setForWorker(int workerId, SessionId sid) noexcept
    {
        assert(workerId >= 0 && workerId < workerCount_);
        assert(hypernet::core::ThreadContext::currentWorkerId() == workerId);

        // [수정] 포인터 접근
        slots_[static_cast<std::size_t>(workerId)]->sid.store(sid, std::memory_order_relaxed);
    }

    [[nodiscard]] SessionId getForWorker(int workerId) const noexcept
    {
        assert(workerId >= 0 && workerId < workerCount_);
        // [수정] 포인터 접근
        return slots_[static_cast<std::size_t>(workerId)]->sid.load(std::memory_order_relaxed);
    }

    [[nodiscard]] SessionId getLocal() const noexcept
    {
        const int wid = hypernet::core::ThreadContext::currentWorkerId();
        assert(wid >= 0 && wid < workerCount_);
        // [수정] 포인터 접근
        return slots_[static_cast<std::size_t>(wid)]->sid.load(std::memory_order_relaxed);
    }

    void clearLocal() noexcept
    {
        const int wid = hypernet::core::ThreadContext::currentWorkerId();
        assert(wid >= 0 && wid < workerCount_);
        // [수정] 포인터 접근
        slots_[static_cast<std::size_t>(wid)]->sid.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] bool isLocal(SessionId sid) const noexcept { return sid != 0 && sid == getLocal(); }

  private:
    struct alignas(64) Slot
    {
        std::atomic<SessionId> sid{0};

        Slot() = default;

        // atomic 특성상 복사/이동 금지
        Slot(const Slot &) = delete;
        Slot &operator=(const Slot &) = delete;
        Slot(Slot &&) = delete;
        Slot &operator=(Slot &&) = delete;

        std::array<std::uint8_t, (64 > sizeof(std::atomic<SessionId>)) ? (64 - sizeof(std::atomic<SessionId>)) : 0> pad{};
    };

    int workerCount_{0};
    // [수정] Slot 객체 대신 포인터 저장
    std::vector<std::unique_ptr<Slot>> slots_{};
};
} // namespace hyperapp