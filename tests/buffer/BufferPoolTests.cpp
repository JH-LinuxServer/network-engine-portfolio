#include <hypernet/buffer/BufferPool.hpp>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using hypernet::buffer::BufferPool;

namespace {

bool test_basic_allocate_deallocate() {
    constexpr std::size_t kBlockSize = 64;
    constexpr std::size_t kBlockCount = 8;

    BufferPool pool(kBlockSize, kBlockCount);

    std::vector<void *> ptrs;
    ptrs.reserve(kBlockCount);

    // capacity 만큼 모두 allocate 하면 nullptr 없이 성공해야 한다.
    for (std::size_t i = 0; i < kBlockCount; ++i) {
        void *p = pool.allocate();
        if (!p) {
            std::cerr << "[basic] allocate returned nullptr at i=" << i << "\n";
            return false;
        }
        ptrs.push_back(p);
    }

    if (pool.freeBlocks() != 0) {
        std::cerr << "[basic] freeBlocks != 0 after full allocate, freeBlocks=" << pool.freeBlocks()
                  << "\n";
        return false;
    }

    // 더 이상 남은 블록이 없으므로 nullptr 이어야 한다.
    if (pool.allocate() != nullptr) {
        std::cerr << "[basic] expected nullptr when exhausted\n";
        return false;
    }

    // 다시 모두 반환
    for (auto *p : ptrs) {
        pool.deallocate(p);
    }

    if (pool.freeBlocks() != kBlockCount) {
        std::cerr << "[basic] freeBlocks != capacity after deallocate, freeBlocks="
                  << pool.freeBlocks() << "\n";
        return false;
    }

    return true;
}

bool test_reuse_blocks() {
    constexpr std::size_t kBlockSize = 32;
    constexpr std::size_t kBlockCount = 4;

    BufferPool pool(kBlockSize, kBlockCount);

    // 1) 한 블록을 할당하여 특정 패턴을 채운 뒤 반환한다.
    void *first = pool.allocate();
    if (!first) {
        std::cerr << "[reuse] first allocate failed\n";
        return false;
    }

    std::memset(first, 0xAB, kBlockSize);
    pool.deallocate(first);

    // 2) 다시 allocate 했을 때, 같은 포인터가 올 수도 있고 아닐 수도 있다.
    //    다만, 메모리 풀 자체가 crash 없이 재사용된다는 것만 확인한다.
    void *second = pool.allocate();
    if (!second) {
        std::cerr << "[reuse] second allocate failed\n";
        return false;
    }

    // second 에 쓰기/읽기를 해본다.
    std::memset(second, 0xCD, kBlockSize);

    // 간단히 몇 바이트만 확인
    auto *bytes = static_cast<unsigned char *>(second);
    for (std::size_t i = 0; i < 4; ++i) {
        if (bytes[i] != 0xCD) {
            std::cerr << "[reuse] memory content mismatch at i=" << i << "\n";
            return false;
        }
    }

    pool.deallocate(second);

    return true;
}

bool test_many_alloc_free_cycles() {
    constexpr std::size_t kBlockSize = 128;
    constexpr std::size_t kBlockCount = 16;
    constexpr std::size_t kLoops = 10000;

    BufferPool pool(kBlockSize, kBlockCount);

    for (std::size_t loop = 0; loop < kLoops; ++loop) {
        std::vector<void *> ptrs;
        ptrs.reserve(kBlockCount);

        // 최대한 많이 할당
        while (void *p = pool.allocate()) {
            ptrs.push_back(p);
        }

        // 최소 1개 이상은 할당되었어야 한다.
        if (ptrs.empty()) {
            std::cerr << "[cycles] no block allocated at loop=" << loop << "\n";
            return false;
        }

        // 다시 모두 반환
        for (void *p : ptrs) {
            pool.deallocate(p);
        }

        if (pool.freeBlocks() != kBlockCount) {
            std::cerr << "[cycles] freeBlocks mismatch after loop=" << loop
                      << " freeBlocks=" << pool.freeBlocks() << "\n";
            return false;
        }
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok = ok && test_basic_allocate_deallocate();
    ok = ok && test_reuse_blocks();
    ok = ok && test_many_alloc_free_cycles();

    if (!ok) {
        std::cerr << "BufferPool tests FAILED\n";
        return 1;
    }

    std::cout << "BufferPool tests PASSED\n";
    return 0;
}
