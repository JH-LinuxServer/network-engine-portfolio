#pragma once

#include <algorithm>
#include <limits>

#include <hypernet/EngineConfig.hpp>
#include <hypernet/core/Options.hpp>

namespace hypernet::core
{

inline EngineOptions makeEffectiveEngineOptions(const hypernet::EngineConfig &cfg)
{
    EngineOptions opt{};
    opt.shutdownDrainTimeout = std::chrono::milliseconds(
        cfg.shutdownDrainTimeoutMs != 0 ? cfg.shutdownDrainTimeoutMs : 3000);

    opt.shutdownPollInterval = std::chrono::milliseconds(
        cfg.shutdownPollIntervalMs != 0 ? cfg.shutdownPollIntervalMs : 50);

    opt.workerCount = hypernet::effectiveWorkerThreads(cfg);
    if (opt.workerCount == 0)
    {
        opt.workerCount = 1;
    }

    // ===== cfg override(0이면 미지정) =====
    if (cfg.listenBacklog != 0)
    {
        const std::uint32_t lim = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
        const std::uint32_t v = (cfg.listenBacklog > lim) ? lim : cfg.listenBacklog;
        opt.listenBacklog = static_cast<int>(v);
    }
    if (cfg.tickResolutionMs != 0)
    {
        opt.workerDefaults.timer.tickResolution =
            TimerWheel::Duration{static_cast<TimerWheel::Duration::rep>(cfg.tickResolutionMs)};
    }
    if (cfg.timerSlots != 0)
    {
        opt.workerDefaults.timer.slotCount = cfg.timerSlots;
    }
    if (cfg.maxEpollEvents != 0)
    {
        const std::uint32_t lim = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
        const std::uint32_t v = (cfg.maxEpollEvents > lim) ? lim : cfg.maxEpollEvents;
        opt.workerDefaults.eventLoop.maxEpollEvents = static_cast<int>(v);
    }
    if (cfg.bufferBlockSize != 0)
    {
        opt.workerDefaults.bufferPool.blockSize = cfg.bufferBlockSize;
    }
    if (cfg.bufferBlockCount != 0)
    {
        opt.workerDefaults.bufferPool.blockCount = cfg.bufferBlockCount;
    }
    if (cfg.recvRingCapacity != 0)
    {
        opt.workerDefaults.rings.recvCapacity = cfg.recvRingCapacity;
    }
    if (cfg.sendRingCapacity != 0)
    {
        opt.workerDefaults.rings.sendCapacity = cfg.sendRingCapacity;
    }
    if (cfg.maxPayloadLen != 0)
    {
        opt.workerDefaults.protocol.maxPayloadLen = cfg.maxPayloadLen;
    }

    // 기본값 방어 (0/음수일 경우 Default 적용)
    if (opt.workerDefaults.timer.tickResolution <= TimerWheel::Duration::zero())
    {
        opt.workerDefaults.timer.tickResolution = TimerWheel::Duration{
            static_cast<TimerWheel::Duration::rep>(defaults::kTickResolutionMs)};
    }
    if (opt.workerDefaults.timer.slotCount == 0)
    {
        opt.workerDefaults.timer.slotCount = defaults::kTimerSlots;
    }
    if (opt.workerDefaults.eventLoop.maxEpollEvents <= 0)
    {
        opt.workerDefaults.eventLoop.maxEpollEvents = defaults::kMaxEpollEvents;
    }
    if (opt.workerDefaults.bufferPool.blockSize == 0)
    {
        opt.workerDefaults.bufferPool.blockSize = defaults::kBufferBlockSize;
    }
    if (opt.workerDefaults.bufferPool.blockCount == 0)
    {
        opt.workerDefaults.bufferPool.blockCount = defaults::kBufferBlockCount;
    }
    if (opt.workerDefaults.rings.recvCapacity == 0)
    {
        opt.workerDefaults.rings.recvCapacity = defaults::kRecvRingCapacity;
    }
    if (opt.workerDefaults.rings.sendCapacity == 0)
    {
        opt.workerDefaults.rings.sendCapacity = defaults::kSendRingCapacity;
    }
    if (opt.workerDefaults.protocol.maxPayloadLen == 0)
    {
        opt.workerDefaults.protocol.maxPayloadLen = defaults::kMaxPayloadLen;
    }

    // ===== 파생/정책 정리 =====
    const std::size_t recvCap = opt.workerDefaults.rings.recvCapacity;
    if (recvCap > 4)
    {
        const std::size_t maxByRing = recvCap - 4;
        const std::uint32_t cap32 = (maxByRing > std::numeric_limits<std::uint32_t>::max())
                                        ? std::numeric_limits<std::uint32_t>::max()
                                        : static_cast<std::uint32_t>(maxByRing);

        opt.workerDefaults.protocol.maxPayloadLen =
            std::min(opt.workerDefaults.protocol.maxPayloadLen, cap32);
    }

    return opt;
}

} // namespace hypernet::core