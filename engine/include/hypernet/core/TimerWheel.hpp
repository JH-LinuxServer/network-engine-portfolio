#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <hypernet/util/NonCopyable.hpp>

namespace hypernet::core {

/// coarse-grained 타이머 휠 구현입니다.
///
/// - 단일 스레드에서 사용된다는 가정 하에 설계되었습니다.
///   (보통 하나의 워커 스레드 / 이벤트 루프가 전담해서 tick()을 호출)
/// - 시간은 고정된 tick 해상도(예: 10ms, 100ms)로 양자화되어 관리됩니다.
///   - addTimer() 에 전달된 duration 은 tick 단위로 올림(ceil)되어 스케줄됩니다.
///   - 실제 콜백 실행 시점은 (요청한 duration) ~ (duration + tickResolution) 사이가 됩니다.
/// - 현재 버전은 **one-shot 타이머**만 지원하며, cancel API 는 제공하지 않습니다.
///   (TimerId 를 미리 도입하여 추후 cancelTimer(id) 를 추가할 수 있도록 설계)
class TimerWheel : private hypernet::util::NonCopyable {
  public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;
    using Callback = std::function<void()>;
    using TimerId = std::uint64_t;

    /// @param tickResolution 각 tick 이 의미하는 시간 (예: 10ms, 100ms). 0보다 커야 합니다.
    /// @param slotCount 타이머 휠 슬롯 개수입니다. 0이면 std::invalid_argument 예외가 발생합니다.
    ///
    /// - slotCount 가 클수록 wheel wrap-around 주기가 길어지고, 매우 먼 미래 타이머가
    ///   같은 슬롯에서 여러 번 검사를 덜 받게 됩니다(대신 메모리 사용량 증가).
    explicit TimerWheel(Duration tickResolution, std::size_t slotCount);

    ~TimerWheel() = default;

    /// tick 해상도(ms 단위)를 반환합니다.
    [[nodiscard]] Duration tickResolution() const noexcept { return tickResolution_; }

    /// 휠의 슬롯 개수를 반환합니다.
    [[nodiscard]] std::size_t slotCount() const noexcept { return slotCount_; }

    /// 현재까지 진행된 tick 인덱스를 반환합니다.
    ///
    /// - 0에서 시작하여 tick() 이 호출될 때마다 1씩 증가합니다.
    [[nodiscard]] std::uint64_t currentTick() const noexcept { return currentTick_; }

    /// 현재 스케줄된(아직 실행되지 않은) 타이머 개수를 반환합니다.
    [[nodiscard]] std::size_t pendingTimers() const noexcept { return activeTimers_; }

    /// 지연 시간(delay) 이후에 한 번 실행될 타이머를 등록합니다.
    ///
    /// - delay 는 0 이상이어야 하며, tickResolution 보다 작더라도 최소 1 tick 뒤에 실행됩니다.
    /// - 콜백은 TimerWheel 를 소유한 스레드(보통 워커/이벤트 루프 스레드)에서 실행됩니다.
    /// - 콜백이 예외를 던지면 std::terminate 로 이어질 수 있으므로,
    ///   콜백 내부에서 예외를 처리하는 것을 권장합니다.
    ///
    /// @return TimerId 예약된 타이머의 식별자입니다. (현재 버전에서는 cancel 등에 사용되지 않음)
    TimerId addTimer(Duration delay, Callback callback);

    /// 한 tick 만큼 타이머 휠을 전진시키고, 만료된 타이머들의 콜백을 실행합니다.
    ///
    /// - 호출 시점이 실제 시간과 정확히 tickResolution 만큼 떨어져 있지 않아도 상관없고,
    ///   단순히 "논리적 tick 이 하나 지났다" 라는 의미만 갖습니다.
    /// - 주로 테스트나, 이미 고정 주기로 돌아가는 루프에서 직접 호출하는 용도입니다.
    void tick();

    /// 마지막 tick() 이후 경과한 실제 시간을 기반으로,
    /// 필요한 만큼 여러 tick 을 한 번에 처리합니다.
    ///
    /// - now 가 이전보다 tickResolution * N 만큼 증가했다면,
    ///   내부적으로 tick()을 N번 호출하는 것과 동일한 효과를 냅니다.
    /// - 실제 이벤트 루프에서는 대략 다음과 같이 사용할 수 있습니다:
    ///   @code
    ///   TimerWheel wheel(10ms, 1024);
    ///   for (;;) {
    ///       auto now = TimerWheel::Clock::now();
    ///       wheel.tick(now);
    ///       // epoll_wait(..., timeout=wheel.tickResolution());
    ///   }
    ///   @endcode
    void tick(Clock::time_point now);

  private:
    struct Timer {
        TimerId id{};
        std::uint64_t expirationTick{};
        Callback callback;
    };

    Duration tickResolution_;
    std::size_t slotCount_{0};
    std::vector<std::vector<Timer>> slots_;

    Clock::time_point lastTickTime_{};
    std::uint64_t currentTick_{0};
    TimerId nextId_{1};
    std::size_t activeTimers_{0};

    // tick 처리 시 현재 슬롯의 타이머를 안전하게 옮겨 담기 위한 임시 버퍼입니다.
    // - 콜백 실행 도중 addTimer()를 호출해도, 이미 가져온 타이머 목록이 깨지지 않도록
    //   슬롯 벡터와 분리된 별도 컨테이너를 사용합니다.
    std::vector<Timer> scratch_;

    /// Duration 을 tick 개수로 변환합니다. (올림(ceil) 처리)
    [[nodiscard]] std::uint64_t durationToTicks(Duration delay) const noexcept;

    /// 새로운 TimerId 를 생성합니다. 0은 유효하지 않은 값으로 예약합니다.
    [[nodiscard]] TimerId nextTimerId() noexcept;

    /// 하나의 tick 을 처리하면서, 만료된 타이머를 실행하고 나머지를 재배치합니다.
    void processCurrentTick();
};

} // namespace hypernet::core
