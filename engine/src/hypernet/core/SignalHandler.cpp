#include <hypernet/core/SignalHandler.hpp>

#include <system_error>

#include <errno.h>
#include <string.h> // strerror

namespace hypernet::core {

namespace {

// async-signal-safe 관점에서 "플래그"는 최대한 단순한 타입이 유리합니다.
// - std::sig_atomic_t는 signal handler에서 안전하게 읽고/쓸 수 있는 타입으로 의도됩니다.
// - C++ 표준 차원에서 std::atomic<T>의 async-signal-safe는 보장되지 않으므로,
//   이 STEP에서는 sig_atomic_t 기반을 선택합니다.
volatile std::sig_atomic_t g_stopRequested = 0;
volatile std::sig_atomic_t g_lastSignal = 0;

} // namespace

SignalHandler::SignalHandler() { installOrThrow(); }

SignalHandler::~SignalHandler() noexcept { uninstall(); }

void SignalHandler::installOrThrow() {
    if (installed_) {
        return;
    }

    // SA_RESTART를 쓰지 않습니다:
    // - SIGINT/SIGTERM으로 epoll_wait/accept 등이 EINTR로 깨어나고,
    //   엔진 루프가 플래그를 관측해 종료로 들어가는 흐름이 더 명확합니다.
    struct sigaction sa{};
    sa.sa_handler = &SignalHandler::handleSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // 중간 실패 시 롤백을 위해 i 단위로 설치합니다.
    for (std::size_t i = 0; i < kSignals.size(); ++i) {
        const int signo = kSignals[i];
        if (::sigaction(signo, &sa, &oldActions_[i]) != 0) {
            // 이미 설치된 것들은 best-effort로 복구
            for (std::size_t j = 0; j < i; ++j) {
                (void)::sigaction(kSignals[j], &oldActions_[j], nullptr);
            }

            // 설치 단계(일반 스레드)에서는 예외를 던져도 괜찮습니다.
            throw std::system_error(errno, std::generic_category(),
                                    "SignalHandler: sigaction failed");
        }
    }

    installed_ = true;
}

void SignalHandler::uninstall() noexcept {
    if (!installed_) {
        return;
    }

    for (std::size_t i = 0; i < kSignals.size(); ++i) {
        (void)::sigaction(kSignals[i], &oldActions_[i], nullptr);
    }

    installed_ = false;
}

void SignalHandler::handleSignal(int signo) noexcept {
    // 절대 금지: 로그, malloc/new, mutex, format, iostream, 대부분의 STL
    g_stopRequested = 1;
    g_lastSignal = signo;
}

bool SignalHandler::isStopRequested() const noexcept { return g_stopRequested != 0; }

bool SignalHandler::consumeStopRequest(int *outSignal) noexcept {
    if (g_stopRequested == 0) {
        return false;
    }

    // 요청을 소비한다(내린다). 신호가 연속으로 오면 마지막 신호만 남을 수 있음.
    g_stopRequested = 0;

    const int signo = static_cast<int>(g_lastSignal);
    g_lastSignal = 0;

    if (outSignal) {
        *outSignal = signo;
    }
    return true;
}

void SignalHandler::reset() noexcept {
    g_stopRequested = 0;
    g_lastSignal = 0;
}

std::string_view SignalHandler::signalName(int signo) noexcept {
    switch (signo) {
    case SIGINT:
        return "SIGINT";
    case SIGTERM:
        return "SIGTERM";
    default:
        return "UNKNOWN";
    }
}

} // namespace hypernet::core
