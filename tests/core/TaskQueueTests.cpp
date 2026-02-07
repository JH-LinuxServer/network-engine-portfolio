#include <hypernet/core/TaskQueue.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using hypernet::core::TaskQueue;

namespace {

/// 단일 스레드 환경에서 FIFO 순서 보장이 되는지 확인하는 기본 테스트입니다.
bool test_single_thread_order() {
    TaskQueue queue;
    std::vector<int> result;

    constexpr int kCount = 5;
    for (int i = 0; i < kCount; ++i) {
        queue.push(TaskQueue::Task([&result, i] { result.push_back(i); }));
    }

    TaskQueue::Task task;
    while (queue.tryPop(task)) {
        task(); // 같은 스레드에서 즉시 실행
    }

    if (result.size() != static_cast<std::size_t>(kCount)) {
        std::cerr << "[single] unexpected result size: " << result.size() << " expected=" << kCount
                  << "\n";
        return false;
    }

    for (int i = 0; i < kCount; ++i) {
        if (result[i] != i) {
            std::cerr << "[single] order mismatch at index " << i << " value=" << result[i]
                      << " expected=" << i << "\n";
            return false;
        }
    }

    return true;
}

/// 여러 producer 스레드와 단일 consumer 스레드 간에 작업이 안전하게 전달되는지 확인합니다.
bool test_multi_producer_single_consumer() {
    TaskQueue queue;

    constexpr int kProducerCount = 4;
    constexpr int kTasksPerProducer = 1000;
    constexpr int kExpectedTasks = kProducerCount * kTasksPerProducer;

    std::atomic<int> executedTasks{0};
    std::atomic<bool> producersDone{false};

    // consumer 스레드는 큐에서 Task 를 꺼내 실행하는 역할만 합니다.
    std::thread consumer([&]() {
        for (;;) {
            TaskQueue::Task task;

            if (queue.tryPop(task)) {
                // Task 실행: 실행된 Task 수를 증가시킨다.
                task();
            } else {
                // 큐가 비어 있고, 모든 producer 가 끝났으며, 기대하는 Task 수를 모두 실행했다면
                // 종료.
                if (producersDone.load(std::memory_order_acquire) &&
                    executedTasks.load(std::memory_order_relaxed) >= kExpectedTasks) {
                    break;
                }

                // 바쁜 루프를 피하기 위해 다른 스레드에 양보한다.
                std::this_thread::yield();
            }
        }
    });

    // 여러 producer 스레드 생성
    std::vector<std::thread> producers;
    producers.reserve(kProducerCount);

    for (int producerId = 0; producerId < kProducerCount; ++producerId) {
        producers.emplace_back([&, producerId]() {
            for (int i = 0; i < kTasksPerProducer; ++i) {
                // 각 Task 는 실행 시 executedTasks 카운터를 1 증가시킨다.
                queue.push(TaskQueue::Task([&executedTasks, producerId, i]() {
                    (void)producerId;
                    (void)i;
                    executedTasks.fetch_add(1, std::memory_order_relaxed);
                }));
            }
        });
    }

    // 모든 producer 가 끝날 때까지 대기
    for (auto &t : producers) {
        t.join();
    }
    producersDone.store(true, std::memory_order_release);

    // consumer 스레드 종료 대기
    consumer.join();

    const int finalCount = executedTasks.load(std::memory_order_relaxed);
    if (finalCount != kExpectedTasks) {
        std::cerr << "[mpsc] executedTasks=" << finalCount << " expected=" << kExpectedTasks
                  << "\n";
        return false;
    }

    // 마지막으로 큐가 비어 있는지 확인
    TaskQueue::Task dummy;
    if (queue.tryPop(dummy)) {
        std::cerr << "[mpsc] queue is not empty after all tasks executed\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok = ok && test_single_thread_order();
    ok = ok && test_multi_producer_single_consumer();

    if (!ok) {
        std::cerr << "TaskQueue tests FAILED\n";
        return 1;
    }

    std::cout << "TaskQueue tests PASSED\n";
    return 0;
}
