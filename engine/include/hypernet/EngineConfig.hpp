#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <hypernet/core/Logger.hpp>

namespace hypernet
{

/// HyperNet 엔진을 위한 설정 파라미터 구조체입니다.
struct EngineConfig
{
    /// 리스닝 소켓을 바인딩할 주소입니다. (예: "0.0.0.0")
    std::string listenAddress = "0.0.0.0";

    /// 리스닝할 TCP 포트 번호입니다.
    std::uint16_t listenPort = 9000;

    /// listen(2) backlog 입니다.
    /// - 0이면 엔진 기본값(defaults::kListenBacklog)을 사용합니다.
    /// - 운영에서 SYN flood/accept burst 대응을 위해 조정할 수 있도록 노출합니다.
    std::uint32_t listenBacklog = 0;

    /// 사용할 워커 스레드의 개수입니다.
    /// - 0이면 std::thread::hardware_concurrency() 기반으로 자동 결정됩니다.
    unsigned int workerThreads = 0;

    /// SO_REUSEPORT 사용 여부(정책 옵션)
    bool reusePort = true;

    /// 로그를 기록할 파일 경로입니다.
    /// - 빈 문자열("")이면 std::clog 또는 프로세스 전역 Logger의 기본 출력만 사용합니다.
    /// - 로깅 설정 반영은 Engine 시작 시점에 수행됩니다.
    std::string logFilePath;

    /// 기본 로그 레벨입니다.
    core::LogLevel logLevel = core::LogLevel::Info;

    /// 메트릭 HTTP 서버가 바인딩될 주소입니다.
    /// - 보안상 기본은 localhost("127.0.0.1") 입니다.
    /// - 외부 노출이 필요하면 "0.0.0.0" 등으로 변경하세요.
    std::string metricsHttpAddress = "127.0.0.1";

    /// 메트릭 HTTP 엔드포인트가 바인딩될 포트입니다.
    /// - 0이면 메트릭 HTTP 서버를 비활성화합니다.
    std::uint16_t metricsHttpPort = 0;

    /// 세션 idle 타임아웃(ms) 입니다.
    std::uint32_t idleTimeoutMs = 60'000; // 기본 60초

    /// heartbeat 메시지 송신 간격(ms) 입니다.
    std::uint32_t heartbeatIntervalMs = 15'000; // 기본 15초

    // ===== Shutdown tuning (0이면 엔진 기본값 사용) =====
    // 운영/대규모 동접에서 “종료 시 세션 드레인 시간”을 조정할 수 있도록 노출합니다.

    /// shutdown drain timeout(ms)
    /// - 0이면 엔진 기본값(3000ms) 사용
    std::uint32_t shutdownDrainTimeoutMs = 0;

    /// shutdown drain polling interval(ms)
    /// - 0이면 엔진 기본값(50ms) 사용
    std::uint32_t shutdownPollIntervalMs = 0;

    // ===== Advanced tuning (0이면 엔진 기본값 사용) =====
    // 운영/부하/지연 요구에 따라 바꿀 가능성이 높은 것들을 conf로 승격합니다.

    /// 워커 타이머 tick 해상도(ms)
    std::uint32_t tickResolutionMs = 0;

    /// 타이머 슬롯 수(휠 크기)
    std::size_t timerSlots = 0;

    /// epoll_wait 1회당 최대 이벤트 수
    std::uint32_t maxEpollEvents = 0;

    /// BufferPool 블록 크기(bytes)
    std::size_t bufferBlockSize = 0;

    /// BufferPool 블록 개수
    std::size_t bufferBlockCount = 0;

    /// 세션 수신 링버퍼 용량(bytes)
    std::size_t recvRingCapacity = 0;

    /// 세션 송신 링버퍼 용량(bytes)
    std::size_t sendRingCapacity = 0;

    /// 프레이밍 payload 최대 길이(bytes)
    std::uint32_t maxPayloadLen = 0;
};

/// EngineConfig 필드 값에 대한 기본 검증을 수행합니다.
void validateEngineConfig(const EngineConfig &config);

/// workerThreads == 0 인 경우 실제 사용할 워커 스레드 수를 계산합니다.
unsigned int effectiveWorkerThreads(const EngineConfig &config) noexcept;

} // namespace hypernet