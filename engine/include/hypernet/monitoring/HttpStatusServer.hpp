#pragma once

#include <hypernet/net/Socket.hpp>
#include <hypernet/util/NonCopyable.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace hypernet::monitoring
{

/// 별도 스레드에서 동작하는 초경량 HTTP 서버.
/// - GET /metrics 만 지원 (Prometheus text exposition)
/// - stop() 호출 시 wakeup pipe로 즉시 poll을 깨워 안전 종료
class HttpStatusServer final : private hypernet::util::NonCopyable
{
  public:
    HttpStatusServer(std::string bindIp, std::uint16_t port) noexcept;
    ~HttpStatusServer() noexcept;

    /// 서버 스레드 시작. 실패 시 false.
    [[nodiscard]] bool start() noexcept;

    /// 종료 요청만(논블로킹). 스레드는 곧 빠져나오며 stopAndJoin에서 join됨.
    void requestStop() noexcept;

    /// 종료 요청 + join + 리소스 정리(안전, idempotent).
    void stopAndJoin() noexcept;

  private:
    void threadMain_() noexcept;
    void acceptLoop_() noexcept;
    void handleClient_(hypernet::net::Socket &&conn) noexcept;

    static bool parseRequestTarget_(const char *buf, std::size_t len, std::string_view &outMethod,
                                    std::string_view &outTarget) noexcept;

    static void sendAll_(hypernet::net::Socket &sock, const char *data, std::size_t len) noexcept;
    static void sendTextResponse_(hypernet::net::Socket &sock, int code, std::string_view reason,
                                  std::string_view contentType, std::string &&body) noexcept;

    void closeWakeupPipe_() noexcept;

  private:
    std::string bindIp_;
    std::uint16_t port_{0};

    std::atomic_bool started_{false};
    std::atomic_bool stopRequested_{false};

    hypernet::net::Socket listenSock_{};

    // poll 깨우기용 pipe
    std::array<int, 2> wakeupPipe_{-1, -1}; // [0]=read, [1]=write

    std::thread th_{};
};

} // namespace hypernet::monitoring
