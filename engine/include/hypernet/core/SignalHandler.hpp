#pragma once

#include <hypernet/util/NonCopyable.hpp>

#include <array>
#include <csignal> // std::sig_atomic_t
#include <string_view>

#include <signal.h> // sigaction, SIGINT, SIGTERM

namespace hypernet::core
{
class SignalHandler : private hypernet::util::NonCopyable
{
  public:
    /// 기본 신호(SIGINT, SIGTERM)에 대한 핸들러를 설치합니다.
    ///
    /// - 실패 시 std::system_error를 던집니다.
    /// - 여러 인스턴스를 동시에 사용하는 것은 권장하지 않습니다(프로세스 전역 상태이기 때문).
    SignalHandler();

    /// 설치한 핸들러를 원래 핸들러로 복구합니다. (best-effort, 예외 없음)
    ~SignalHandler() noexcept;

    SignalHandler(SignalHandler &&) = delete;
    SignalHandler &operator=(SignalHandler &&) = delete;

    /// 종료가 요청되었는지(플래그가 올라왔는지) 확인합니다.
    [[nodiscard]] bool isStopRequested() const noexcept;

    /// 종료 요청을 "소비(consume)"합니다.
    ///
    /// - 요청이 있었다면 플래그를 내리고 true를 반환합니다.
    /// - outSignal이 제공되면, 마지막으로 관측된 신호 번호를 함께 반환합니다.
    ///
    /// 주의:
    /// - 신호가 연속으로 들어오면 "마지막 신호 번호"만 남을 수 있습니다.
    bool consumeStopRequest(int *outSignal = nullptr) noexcept;

    /// 플래그/마지막 신호 번호를 리셋합니다. (테스트/재시도용)
    void reset() noexcept;

    /// 사람이 읽기 좋은 신호 이름을 반환합니다. (루프 스레드에서만 사용)
    [[nodiscard]] static std::string_view signalName(int signo) noexcept;

  private:
    static void handleSignal(int signo) noexcept;

    // SIGINT/SIGTERM 고정 (현재 STEP의 요구사항)
    static constexpr std::array<int, 2> kSignals = {SIGINT, SIGTERM};

    std::array<struct sigaction, kSignals.size()> oldActions_{};
    bool installed_{false};

    void installOrThrow();
    void uninstall() noexcept;
};

} // namespace hypernet::core
