#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace hypernet::monitoring
{

struct EngineMetricsSnapshot
{
    std::uint64_t currentConnections = 0;
    std::uint64_t rxMessagesTotal = 0;
    std::uint64_t txMessagesTotal = 0;
    std::uint64_t errorsTotal = 0;
    std::uint64_t connectorPending = 0;
    std::uint64_t connectorTotal = 0;
    std::uint64_t connectorSuccessTotal = 0;
    std::uint64_t connectorTimeoutTotal = 0;
    std::uint64_t connectorFailureTotal = 0;
};

class EngineMetrics
{
  public:
    EngineMetrics() = default;
    EngineMetrics(const EngineMetrics &) = delete;
    EngineMetrics &operator=(const EngineMetrics &) = delete;

    void reset() noexcept
    {
        currentConnections_.store(0, std::memory_order_relaxed);
        rxMessagesTotal_.store(0, std::memory_order_relaxed);
        txMessagesTotal_.store(0, std::memory_order_relaxed);
        errorsTotal_.store(0, std::memory_order_relaxed);

        connectorPending_.store(0, std::memory_order_relaxed);
        connectorTotal_.store(0, std::memory_order_relaxed);
        connectorSuccessTotal_.store(0, std::memory_order_relaxed);
        connectorTimeoutTotal_.store(0, std::memory_order_relaxed);
        connectorFailureTotal_.store(0, std::memory_order_relaxed);
    }

    void onConnectionOpened() noexcept
    {
        currentConnections_.fetch_add(1, std::memory_order_relaxed);
    }
    void onConnectionClosed() noexcept
    {
        currentConnections_.fetch_sub(1, std::memory_order_relaxed);
    }
    void onRxMessage() noexcept { rxMessagesTotal_.fetch_add(1, std::memory_order_relaxed); }
    void onTxMessage() noexcept { txMessagesTotal_.fetch_add(1, std::memory_order_relaxed); }
    void onError() noexcept { errorsTotal_.fetch_add(1, std::memory_order_relaxed); }

    void onConnectorTotal() noexcept { connectorTotal_.fetch_add(1, std::memory_order_relaxed); }
    void onConnectorPendingInc() noexcept
    {
        connectorPending_.fetch_add(1, std::memory_order_relaxed);
    }
    void onConnectorPendingDec() noexcept
    {
        connectorPending_.fetch_sub(1, std::memory_order_relaxed);
    }
    void onConnectorSuccess() noexcept
    {
        connectorSuccessTotal_.fetch_add(1, std::memory_order_relaxed);
    }
    void onConnectorTimeout() noexcept
    {
        connectorTimeoutTotal_.fetch_add(1, std::memory_order_relaxed);
    }
    void onConnectorFailure() noexcept
    {
        connectorFailureTotal_.fetch_add(1, std::memory_order_relaxed);
    }

    EngineMetricsSnapshot snapshot() const noexcept;
    std::string toPrometheusText() const;

  private:
    std::atomic<std::int64_t> currentConnections_{0};
    std::atomic<std::uint64_t> rxMessagesTotal_{0};
    std::atomic<std::uint64_t> txMessagesTotal_{0};
    std::atomic<std::uint64_t> errorsTotal_{0};

    // connector
    std::atomic<std::int64_t> connectorPending_{0};
    std::atomic<std::uint64_t> connectorTotal_{0};
    std::atomic<std::uint64_t> connectorSuccessTotal_{0};
    std::atomic<std::uint64_t> connectorTimeoutTotal_{0};
    std::atomic<std::uint64_t> connectorFailureTotal_{0};
};

EngineMetrics &engineMetrics() noexcept;

} // namespace hypernet::monitoring