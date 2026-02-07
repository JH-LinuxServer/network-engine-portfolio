#include <hypernet/buffer/BufferPool.hpp>

#include <algorithm> // std::find (디버그용)
#include <cassert>
#include <cstring>   // std::memset (선택적 초기화 용도)
#include <stdexcept> // std::invalid_argument

namespace hypernet::buffer
{

BufferPool::BufferPool(std::size_t blockSize, std::size_t blockCount)
    : blockSize_(blockSize), blockCount_(blockCount), storage_(nullptr)
{
    if (blockSize_ == 0)
    {
        throw std::invalid_argument("BufferPool blockSize must be greater than 0");
    }
    if (blockCount_ == 0)
    {
        throw std::invalid_argument("BufferPool blockCount must be greater than 0");
    }

    // 전체 블록을 한 번에 할당한다.
    storage_ = std::make_unique<std::byte[]>(blockSize_ * blockCount_);

    // 초기에는 모든 블록이 free 상태이므로 free list 에 등록한다.
    freeList_.reserve(blockCount_);
    for (std::size_t i = 0; i < blockCount_; ++i)
    {
        void *ptr = storage_.get() + (i * blockSize_);
        freeList_.push_back(ptr);

        // 초기화를 원하면 여기에서 std::memset(ptr, 0, blockSize_) 를 호출할 수 있지만,
        // 현재는 성능을 위해 초기화하지 않는다.
    }
}

BufferPool::~BufferPool() = default;

void *BufferPool::allocate() noexcept
{
    if (freeList_.empty())
    {
        // 풀 고갈: nullptr 반환. 상위에서 fall-back (예: malloc) 을 사용할지 결정한다.
        return nullptr;
    }

    void *ptr = freeList_.back();
    freeList_.pop_back();
    return ptr;
}

void BufferPool::deallocate(void *ptr) noexcept
{
    if (!ptr)
    {
        return;
    }

#ifndef NDEBUG
    // 디버그용 방어 로직:
    // 1) 이 풀의 storage 범위 안에 있는지 확인
    const auto base = storage_.get();
    const auto end = base + (blockSize_ * blockCount_);
    auto bytePtr = static_cast<std::byte *>(ptr);

    assert(bytePtr >= base && bytePtr < end && "pointer not in BufferPool storage");

    // 2) 블록 경계에 맞춰 떨어지는지 확인
    const auto offset = static_cast<std::size_t>(bytePtr - base);
    assert(offset % blockSize_ == 0 && "pointer not aligned to block boundary");

    // 3) (선택) 중복 해제 방지: freeList_ 에 이미 존재하는지 검사
    //    O(n)이지만 테스트/개발 단계에서 blockCount 가 크지 않다고 가정.
    auto it = std::find(freeList_.begin(), freeList_.end(), ptr);
    assert(it == freeList_.end() && "double free detected in BufferPool");
#endif

    // 실제 운영에서는 성능을 위해 단순히 push_back 만 수행한다.
    freeList_.push_back(ptr);
}

} // namespace hypernet::buffer
