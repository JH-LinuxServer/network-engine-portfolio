#pragma once

#include <cstddef>
#include <cstdint>

#include <hypernet/core/Defaults.hpp>
#include <hypernet/core/TimerWheel.hpp>

namespace hypernet::core
{

struct TimerOptions
{
    TimerWheel::Duration tickResolution{
        TimerWheel::Duration{static_cast<TimerWheel::Duration::rep>(defaults::kTickResolutionMs)}};
    std::size_t slotCount{defaults::kTimerSlots};
};

struct BufferPoolOptions
{
    std::size_t blockSize{defaults::kBufferBlockSize};
    std::size_t blockCount{defaults::kBufferBlockCount};
};

struct EventLoopOptions
{
    int maxEpollEvents{defaults::kMaxEpollEvents};
};

struct RingBufferOptions
{
    std::size_t recvCapacity{defaults::kRecvRingCapacity};
    std::size_t sendCapacity{defaults::kSendRingCapacity};
};

struct ProtocolOptions
{
    std::uint32_t maxPayloadLen{defaults::kMaxPayloadLen};
};

struct WorkerOptions
{
    unsigned int id{0};
    TimerOptions timer{};
    BufferPoolOptions bufferPool{};
    EventLoopOptions eventLoop{};
    RingBufferOptions rings{};
    ProtocolOptions protocol{};
    std::uint32_t idleTimeoutMs{0};
    std::uint32_t heartbeatIntervalMs{0};
};

struct EngineOptions
{
    unsigned int workerCount{1};
    int listenBacklog{defaults::kListenBacklog};
    WorkerOptions workerDefaults{};
    std::chrono::milliseconds shutdownDrainTimeout;
    std::chrono::milliseconds shutdownPollInterval;
};

inline WorkerOptions makeWorkerOptions(const EngineOptions &opt, unsigned int workerId)
{
    WorkerOptions w = opt.workerDefaults;
    w.id = workerId;
    return w;
}

} // namespace hypernet::core
