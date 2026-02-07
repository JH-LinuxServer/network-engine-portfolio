#pragma once

#include <cstddef> // std::size_t, std::byte
#include <memory>
#include <vector>

#include <hypernet/util/NonCopyable.hpp>

namespace hypernet::buffer {

/// 고정 크기 블록을 관리하는 단일 스레드용 메모리 풀입니다.
///
/// - blockSize() 크기의 블록을 capacity() 개 만큼 미리 할당해 두고,
///   allocate()/deallocate() 로 재사용합니다.
/// - 동시성 제어가 전혀 없으므로, "per-thread" 용도로 사용해야 합니다.
///   (여러 스레드에서 공유하려면 상위에서 스레드별 인스턴스를 따로 만들어야 합니다.)
/// - 첫 버전은 단순한 std::vector<void*> free list 기반 구현이며,
///   나중에 lock-free, NUMA aware 구현 등으로 교체하더라도 인터페이스는 유지할 수 있습니다.
/// - Session 의 recv/send 버퍼, Connector 인코딩 버퍼, 모니터링/로깅 payload 등
///   "한 워커가 자주 할당/해제하는 고정 크기 버퍼"를 이 풀에서 뽑아 쓰는 것을 기본 그림으로 합니다.
class BufferPool : private hypernet::util::NonCopyable {
  public:
    /// @param blockSize 각 블록의 크기(바이트 단위, 0 불가)
    /// @param blockCount 풀에서 관리할 블록 개수(0 불가)
    BufferPool(std::size_t blockSize, std::size_t blockCount);

    ~BufferPool();

    /// 사용 가능한 블록 하나를 할당합니다.
    ///
    /// - 성공 시: blockSize() 바이트 크기의 메모리 블록 포인터를 반환합니다.
    /// - 풀에 사용 가능한 블록이 더 이상 없으면 nullptr 을 반환합니다.
    /// - 반환된 포인터는 반드시 deallocate() 로 다시 돌려줘야 합니다.
    [[nodiscard]] void *allocate() noexcept;

    /// allocate() 로 얻은 블록을 풀에 반환합니다.
    ///
    /// - ptr == nullptr 이면 아무 일도 하지 않습니다.
    /// - ptr 이 이 풀에서 관리하는 블록이 아닌 경우는 UB 이지만,
    ///   디버그 빌드에서는 assert 로 잡도록 구현합니다.
    void deallocate(void *ptr) noexcept;

    /// 각 블록의 크기(바이트 단위)입니다.
    [[nodiscard]] std::size_t blockSize() const noexcept { return blockSize_; }

    /// 풀에 포함된 총 블록 개수입니다.
    [[nodiscard]] std::size_t capacity() const noexcept { return blockCount_; }

    /// 현재 free list 에 남아 있는 블록 개수입니다.
    [[nodiscard]] std::size_t freeBlocks() const noexcept { return freeList_.size(); }

    /// 이미 모든 블록이 사용 중인지 여부를 반환합니다.
    [[nodiscard]] bool exhausted() const noexcept { return freeBlocks() == 0; }

  private:
    std::size_t blockSize_{0};
    std::size_t blockCount_{0};

    // 실제 메모리 블록을 보관하는 연속 버퍼입니다.
    // - 각 블록은 storage_.get() + i * blockSize_ 위치에 존재합니다.
    std::unique_ptr<std::byte[]> storage_;

    // free list: 현재 사용 가능 블록들의 포인터를 스택처럼 관리합니다.
    std::vector<void *> freeList_;
};

} // namespace hypernet::buffer
