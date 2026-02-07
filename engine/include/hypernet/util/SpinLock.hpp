#pragma once

#include <atomic>

namespace hypernet::util {

/// 매우 짧은 크리티컬 섹션에서 사용할 수 있는 간단한 스핀락 구현입니다.
///
/// - std::atomic_flag 를 이용해 (busy-wait) 방식으로 동작합니다.
/// - 잠금 구간에서는 동적 할당 / I/O / syscalls / 긴 연산 등을 최대한 피해야 합니다.
/// - worker 간 공유되는 작은 카운터/플래그 보호 등에 적합합니다.
class SpinLock {
  public:
    SpinLock() noexcept = default;

    SpinLock(const SpinLock &) = delete;
    SpinLock &operator=(const SpinLock &) = delete;

    /// 잠금을 획득할 때까지 바쁜 루프를 돌며 대기합니다.
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // 아주 단순한 스핀. 필요 시 std::this_thread::yield() 등을 추가할 수 있습니다.
        }
    }

    /// 잠금 획득을 시도하고, 성공 여부를 반환합니다.
    bool try_lock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }

    /// 잠금을 해제합니다.
    void unlock() noexcept { flag_.clear(std::memory_order_release); }

  private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

/// RAII 기반 스핀락 가드입니다.
/// 생성 시 lock(), 소멸 시 unlock()을 호출합니다.
class SpinLockGuard {
  public:
    explicit SpinLockGuard(SpinLock &lock) noexcept : lock_(lock) { lock_.lock(); }
    ~SpinLockGuard() { lock_.unlock(); }

    SpinLockGuard(const SpinLockGuard &) = delete;
    SpinLockGuard &operator=(const SpinLockGuard &) = delete;

  private:
    SpinLock &lock_;
};

} // namespace hypernet::util