#include <hypernet/core/TimerWheel.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>

using hypernet::core::TimerWheel;

namespace {

/// 단일 타이머가 대략 기대한 tick 에서 한 번만 실행되는지 확인합니다.
bool test_single_timer_basic() {
    using namespace std::chrono_literals;

    TimerWheel wheel(10ms, 8); // 10ms tick, 8-slot wheel

    bool fired = false;
    wheel.addTimer(25ms, [&]() { fired = true; });

    // 25ms -> tickResolution=10ms 기준으로 3 tick 에서 실행되어야 한다.
    // tick 1, 2 에서는 fired=false 이어야 한다.
    for (int i = 0; i < 2; ++i) {
        wheel.tick();
        if (fired) {
            std::cerr << "[single] timer fired too early at tick " << (i + 1) << "\n";
            return false;
        }
    }

    wheel.tick(); // tick 3

    if (!fired) {
        std::cerr << "[single] timer did not fire at expected tick\n";
        return false;
    }

    // 이후 tick 에서는 더 이상 실행되면 안 된다.
    fired = false;
    wheel.tick();
    if (fired) {
        std::cerr << "[single] timer fired more than once\n";
        return false;
    }

    return true;
}

/// 여러 타이머를 서로 다른 delay 로 등록했을 때, 각 타이머가
/// 서로 다른 tick 에서 한 번씩만 실행되는지 확인합니다.
bool test_multiple_timers_order() {
    using namespace std::chrono_literals;

    TimerWheel wheel(10ms, 16);

    std::uint64_t currentTick = 0;
    int firedA = 0;
    int firedB = 0;
    int firedC = 0;

    // 현재 tick 을 외부에서 관리하면서, 콜백이 어느 tick 에서 실행되는지 기록
    wheel.addTimer(10ms, [&]() {
        ++firedA;
        if (currentTick != 1) {
            std::cerr << "[multi] timer A fired at tick " << currentTick << " (expected 1)\n";
        }
    });

    wheel.addTimer(35ms, [&]() {
        ++firedB;
        if (currentTick < 3 || currentTick > 4) {
            std::cerr << "[multi] timer B fired at tick " << currentTick << " (expected ~4)\n";
        }
    });

    wheel.addTimer(70ms, [&]() {
        ++firedC;
        if (currentTick < 6 || currentTick > 8) {
            std::cerr << "[multi] timer C fired at tick " << currentTick << " (expected ~7)\n";
        }
    });

    // 총 10 tick 진행
    for (int i = 0; i < 10; ++i) {
        ++currentTick;
        wheel.tick();
    }

    if (firedA != 1 || firedB != 1 || firedC != 1) {
        std::cerr << "[multi] fired counts: A=" << firedA << " B=" << firedB << " C=" << firedC
                  << " (expected 1 each)\n";
        return false;
    }

    return true;
}

/// slotCount 보다 긴 delay 를 가진 타이머가 wheel wrap-around 후에도
/// 정상적으로 실행되는지 확인합니다.
bool test_wrap_around() {
    using namespace std::chrono_literals;

    // slotCount 가 작은 휠: wrap-around 를 자주 발생시키기 위한 설정
    TimerWheel wheel(10ms, 4);

    std::uint64_t currentTick = 0;
    int firedShort = 0;
    int firedLong = 0;

    // 2 tick 뒤에 실행되는 타이머
    wheel.addTimer(20ms, [&]() {
        ++firedShort;
        if (currentTick != 2) {
            std::cerr << "[wrap] short timer fired at tick " << currentTick << " (expected 2)\n";
        }
    });

    // 9 tick 뒤에 실행되는 타이머 (여러 번 wrap-around 를 지나야 함)
    wheel.addTimer(90ms, [&]() {
        ++firedLong;
        if (currentTick < 8 || currentTick > 10) {
            std::cerr << "[wrap] long timer fired at tick " << currentTick << " (expected ~9)\n";
        }
    });

    // 총 12 tick 진행
    for (int i = 0; i < 12; ++i) {
        ++currentTick;
        wheel.tick();
    }

    if (firedShort != 1 || firedLong != 1) {
        std::cerr << "[wrap] fired counts: short=" << firedShort << " long=" << firedLong
                  << " (expected 1 each)\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok = ok && test_single_timer_basic();
    ok = ok && test_multiple_timers_order();
    ok = ok && test_wrap_around();

    if (!ok) {
        std::cerr << "TimerWheel tests FAILED\n";
        return 1;
    }

    std::cout << "TimerWheel tests PASSED\n";
    return 0;
}
