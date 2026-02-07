#include <hypernet/core/TimerWheel.hpp>

#include <stdexcept>
#include <utility>

namespace hypernet::core
{

TimerWheel::TimerWheel(Duration tickResolution, std::size_t slotCount)
    : tickResolution_(tickResolution), slotCount_(slotCount), slots_(slotCount),
      lastTickTime_(Clock::now())
{
    if (tickResolution_ <= Duration::zero())
    {
        throw std::invalid_argument("TimerWheel tickResolution must be > 0");
    }
    if (slotCount_ == 0)
    {
        throw std::invalid_argument("TimerWheel slotCount must be > 0");
    }
}

TimerWheel::TimerId TimerWheel::addTimer(Duration delay, Callback callback)
{
    if (!callback)
    {
        throw std::invalid_argument("TimerWheel::addTimer requires a valid callback");
    }

    const auto ticks = durationToTicks(delay);
    // 최소 1 tick 뒤에 실행되도록 강제한다.
    const std::uint64_t delayTicks = (ticks == 0) ? 1 : ticks;

    const auto expirationTick = currentTick_ + delayTicks;
    const auto slotIndex = static_cast<std::size_t>(expirationTick % slotCount_);

    const TimerId id = nextTimerId();
    slots_[slotIndex].push_back(Timer{id, expirationTick, std::move(callback)});
    ++activeTimers_;

    return id;
}

void TimerWheel::tick()
{
    // 논리 tick 한 번 진행
    ++currentTick_;
    processCurrentTick();
}

void TimerWheel::tick(Clock::time_point now)
{
    if (now <= lastTickTime_)
    {
        return; // 시간 변화 없음(또는 역행): 아무 것도 하지 않는다.
    }

    const auto elapsed = now - lastTickTime_;
    const auto elapsedMs = std::chrono::duration_cast<Duration>(elapsed);

    if (elapsedMs < tickResolution_)
    {
        // 아직 한 tick 을 진행할 만큼의 시간이 지나지 않았다.
        return;
    }

    const auto totalMs = static_cast<std::uint64_t>(elapsedMs.count());
    const auto tickMs = static_cast<std::uint64_t>(tickResolution_.count());
    const auto ticksToAdvance = totalMs / tickMs;

    for (std::uint64_t i = 0; i < ticksToAdvance; ++i)
    {
        tick();
    }

    // lastTickTime_ 을 tickResolution * ticksToAdvance 만큼 앞으로 이동시켜,
    // 누적 오차를 줄인다.
    lastTickTime_ += tickResolution_ * static_cast<int64_t>(ticksToAdvance);
}

std::uint64_t TimerWheel::durationToTicks(Duration delay) const noexcept
{
    if (delay <= Duration::zero())
    {
        return 0;
    }

    const auto delayMs = static_cast<std::uint64_t>(delay.count());
    const auto tickMs = static_cast<std::uint64_t>(tickResolution_.count());

    // 올림(ceil) 연산: (delayMs + tickMs - 1) / tickMs
    const auto ticks = (delayMs + tickMs - 1) / tickMs;
    return ticks;
}

TimerWheel::TimerId TimerWheel::nextTimerId() noexcept
{
    TimerId id = nextId_;
    ++nextId_;
    if (nextId_ == 0)
    {
        // 0은 "invalid" 로 예약
        nextId_ = 1;
    }
    return id;
}

void TimerWheel::processCurrentTick()
{
    const auto slotIndex = static_cast<std::size_t>(currentTick_ % slotCount_);
    auto &bucket = slots_[slotIndex];

    if (bucket.empty())
    {
        return;
    }

    // 현재 슬롯의 타이머들을 scratch_ 로 옮긴 뒤, bucket 을 비워둔다.
    // 이렇게 하면 콜백 실행 도중 addTimer() 가 같은 슬롯에 타이머를 추가해도
    // 이번 tick 에서 처리 중인 목록이 깨지지 않는다.
    scratch_.clear();
    scratch_.reserve(bucket.size());
    scratch_.swap(bucket); // scratch_ <- 기존 bucket, bucket 은 비워짐

    for (auto &timer : scratch_)
    {
        if (timer.expirationTick <= currentTick_)
        {
            // 만료된 타이머: 콜백 실행
            if (timer.callback)
            {
                timer.callback();
            }
            if (activeTimers_ > 0)
            {
                --activeTimers_;
            }
        }
        else
        {
            // 아직 만료되지 않은 타이머: 원래 예정된 슬롯으로 다시 배치
            const auto idx = static_cast<std::size_t>(timer.expirationTick % slotCount_);
            slots_[idx].push_back(std::move(timer));
        }
    }

    // scratch_ 내용은 다음 tick 에서 재사용될 수 있도록 그대로 두되,
    // 다음 processCurrentTick 호출 시 clear() 로 비우게 된다.
}

} // namespace hypernet::core
