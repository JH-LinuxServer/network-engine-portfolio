#include <hypernet/EngineConfig.hpp>

#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace hypernet
{

namespace
{
std::string makeErrorMessage(const std::string &detail)
{
    return "[EngineConfig] " + detail;
}

[[noreturn]] void throwConfigError(const std::string &detail)
{
    auto msg = makeErrorMessage(detail);
    SLOG_ERROR("EngineConfig", "ValidationError", "msg={}", msg);
    throw std::invalid_argument{msg};
}
} // namespace

void validateEngineConfig(const EngineConfig &config)
{
    if (config.listenAddress.empty() && config.listenPort != 0)
    {
        throwConfigError("listenAddress must not be empty when listenPort != 0");
    }

    // listenPort==0 => client-only mode (no listener)
    if (config.listenPort > 65535)
    {
        throwConfigError("listenPort must be in [0, 65535]");
    }

    if (config.metricsHttpPort != 0 && config.metricsHttpPort == config.listenPort &&
        config.listenPort != 0)
    {
        throwConfigError("metricsHttpPort must not be equal to listenPort");
    }

    if (config.workerThreads != 0 && config.workerThreads < 1)
    {
        throwConfigError("workerThreads must be >= 1 when specified");
    }

    // (기존 검증들 그대로 유지: tickResolutionMs/timerSlots/maxEpollEvents/buffer... 등)
    if (config.tickResolutionMs != 0 && config.tickResolutionMs < 1)
    {
        throwConfigError("tickResolutionMs must be >= 1 when specified");
    }
    if (config.timerSlots != 0 && config.timerSlots < 1)
    {
        throwConfigError("timerSlots must be >= 1 when specified");
    }
    if (config.maxEpollEvents != 0 && config.maxEpollEvents < 1)
    {
        throwConfigError("maxEpollEvents must be >= 1 when specified");
    }
    if (config.bufferBlockSize != 0 && config.bufferBlockSize < 256)
    {
        throwConfigError("bufferBlockSize is too small (min 256 bytes when specified)");
    }
    if (config.bufferBlockCount != 0 && config.bufferBlockCount < 1)
    {
        throwConfigError("bufferBlockCount must be >= 1 when specified)");
    }
    if (config.recvRingCapacity != 0 && config.recvRingCapacity < 1024)
    {
        throwConfigError("recvRingCapacity is too small (min 1024 bytes when specified)");
    }
    if (config.sendRingCapacity != 0 && config.sendRingCapacity < 1024)
    {
        throwConfigError("sendRingCapacity is too small (min 1024 bytes when specified)");
    }
    if (config.maxPayloadLen != 0 && config.maxPayloadLen < 1)
    {
        throwConfigError("maxPayloadLen must be >= 1 when specified");
    }

    const unsigned int workers = effectiveWorkerThreads(config);

    // [변경] SO_REUSEPORT 강제는 "리스너를 실제로 켠 경우"에만 의미가 있다.
    if (config.listenPort != 0 && workers > 1 && !config.reusePort)
    {
        throwConfigError(
            "reusePort must be enabled when effective workerThreads > 1 (SO_REUSEPORT required)");
    }
}

unsigned int effectiveWorkerThreads(const EngineConfig &config) noexcept
{
    if (config.workerThreads != 0)
    {
        return config.workerThreads;
    }
    auto hw = std::thread::hardware_concurrency();
    if (hw == 0)
    {
        hw = 1;
    }
    return hw;
}

} // namespace hypernet