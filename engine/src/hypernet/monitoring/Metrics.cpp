#include <hypernet/monitoring/Metrics.hpp>

#include <algorithm>
#include <cinttypes>
#include <sstream>

namespace hypernet::monitoring
{
namespace
{
constexpr const char *kMCurrentConnections = "hypernet_engine_current_connections";
constexpr const char *kMRxMessagesTotal = "hypernet_engine_rx_messages_total";
constexpr const char *kMTxMessagesTotal = "hypernet_engine_tx_messages_total";
constexpr const char *kMErrorsTotal = "hypernet_engine_errors_total";

constexpr const char *kMConnectorPending = "hypernet_connector_pending";
constexpr const char *kMConnectorTotal = "hypernet_connector_total";
constexpr const char *kMConnectorSuccessTotal = "hypernet_connector_success_total";
constexpr const char *kMConnectorTimeoutTotal = "hypernet_connector_timeout_total";
constexpr const char *kMConnectorFailureTotal = "hypernet_connector_failure_total";

inline std::uint64_t clampNonNegative(std::int64_t v) noexcept
{
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, v));
}

inline void appendGauge(std::ostringstream &os, const char *name, const char *help,
                        std::uint64_t value)
{
    os << "# HELP " << name << " " << help << "\n";
    os << "# TYPE " << name << " gauge\n";
    os << name << " " << value << "\n";
}

inline void appendCounter(std::ostringstream &os, const char *name, const char *help,
                          std::uint64_t value)
{
    os << "# HELP " << name << " " << help << "\n";
    os << "# TYPE " << name << " counter\n";
    os << name << " " << value << "\n";
}
} // namespace

EngineMetricsSnapshot EngineMetrics::snapshot() const noexcept
{
    EngineMetricsSnapshot s{};
    s.currentConnections = clampNonNegative(currentConnections_.load(std::memory_order_relaxed));
    s.rxMessagesTotal = rxMessagesTotal_.load(std::memory_order_relaxed);
    s.txMessagesTotal = txMessagesTotal_.load(std::memory_order_relaxed);
    s.errorsTotal = errorsTotal_.load(std::memory_order_relaxed);

    s.connectorPending = clampNonNegative(connectorPending_.load(std::memory_order_relaxed));
    s.connectorTotal = connectorTotal_.load(std::memory_order_relaxed);
    s.connectorSuccessTotal = connectorSuccessTotal_.load(std::memory_order_relaxed);
    s.connectorTimeoutTotal = connectorTimeoutTotal_.load(std::memory_order_relaxed);
    s.connectorFailureTotal = connectorFailureTotal_.load(std::memory_order_relaxed);
    return s;
}

std::string EngineMetrics::toPrometheusText() const
{
    const EngineMetricsSnapshot s = snapshot();

    std::ostringstream os;
    // Engine-level
    appendGauge(os, kMCurrentConnections, "Current number of active TCP sessions.",
                s.currentConnections);
    appendCounter(os, kMRxMessagesTotal,
                  "Total number of framed messages received (dispatcher level).",
                  s.rxMessagesTotal);
    appendCounter(os, kMTxMessagesTotal, "Total number of framed messages sent (dispatcher level).",
                  s.txMessagesTotal);
    appendCounter(os, kMErrorsTotal, "Total number of engine/network errors.", s.errorsTotal);

    // Connector-level (dialer)
    appendGauge(os, kMConnectorPending, "Number of in-flight connector attempts.",
                s.connectorPending);
    appendCounter(os, kMConnectorTotal, "Total connector attempts.", s.connectorTotal);
    appendCounter(os, kMConnectorSuccessTotal, "Total successful connector attempts.",
                  s.connectorSuccessTotal);
    appendCounter(os, kMConnectorTimeoutTotal, "Total timed-out connector attempts.",
                  s.connectorTimeoutTotal);
    appendCounter(os, kMConnectorFailureTotal, "Total failed connector attempts.",
                  s.connectorFailureTotal);

    return os.str();
}

EngineMetrics &engineMetrics() noexcept
{
    static EngineMetrics g;
    return g;
}

} // namespace hypernet::monitoring
