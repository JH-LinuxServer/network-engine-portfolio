#pragma once

#include <hypernet/SessionHandle.hpp>
#include <hypernet/connector/IConnector.hpp>
#include <hypernet/protocol/Dispatcher.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hypernet::net
{
class EventLoop;
class SessionManager;
} // namespace hypernet::net

namespace hypernet::connector
{

// ===========================================================
// Outbound TCP Dial (simple, non-blocking, no DNS)
// ===========================================================
struct DialTcpOptions
{
    // numeric IP only (e.g., "10.0.0.12")
    std::string host;
    std::uint16_t port{0};

    std::uint32_t timeoutMs{3000};   // per-attempt connect timeout (0 = no timeout)
    std::uint32_t retryDelayMs{200}; // delay before retry attempt (only when retryOnce=true)

    bool retryOnce{false}; // at most one retry (total attempts: 1 or 2)
    bool tcpNoDelay{true};
};

using DialTcpCallback = std::function<void(bool ok, hypernet::SessionHandle session, std::string err)>;

// ===========================================================
// ConnectorManager
// ===========================================================
class ConnectorManager
{
  public:
    ConnectorManager(hypernet::net::EventLoop &loop, hypernet::net::SessionManager &sm) noexcept;

    bool add(std::unique_ptr<IConnector> c);

    void sendAsync(std::string_view name, const SendOptions &opt, std::vector<std::uint8_t> request, Callback cb);

    void sendAsyncToDispatcher(std::string_view name, const SendOptions &opt, hypernet::SessionHandle session, hypernet::protocol::Dispatcher::OpCode resumeOpcode, std::vector<std::uint8_t> request);

    // -------------------------------------------------------
    // Outbound TCP Dial API (moved from SessionManager)
    // -------------------------------------------------------
    void dialTcpSession(DialTcpOptions opt, DialTcpCallback cb) noexcept;

    // Called by SessionManager::shutdownInOwnerThread() to avoid fd leaks.
    void shutdownDialsInOwnerThread() noexcept;

  private:
    IConnector *find_(std::string_view name) noexcept;

    // ===== STEP 18-2: pending table + request id + timeout/(optional)retryOnce =====
    using RequestId = std::uint64_t;

    struct Pending
    {
        std::string connectorName;
        SendOptions opt;
        std::vector<std::uint8_t> request; // retry를 위해 원본 보관
        Callback cb;

        int attempt{0}; // 0=first, 1=retryOnce
        std::chrono::milliseconds timeoutResolved{0};
    };

    [[nodiscard]] std::chrono::milliseconds resolveTimeout_(const SendOptions &opt) const noexcept;
    void startAttempt_(RequestId id) noexcept;
    void onTimeout_(RequestId id, int expectedAttempt) noexcept;
    void onComplete_(RequestId id, int attempt, Response &&r) noexcept;

    // ===== TCP Dial internals =====
    using DialId = std::uint64_t;
    struct DialState; // defined in .cpp

    void dialStartAttempt_(DialId id, std::uint32_t attemptIndex) noexcept;
    void dialTimeout_(DialId id, std::uint32_t expectedAttempt) noexcept;
    void finishDialOk_(DialId id, std::uint32_t attemptIndex) noexcept;
    void finishDialFail_(DialId id, std::uint32_t attemptIndex, std::string err, bool isTimeout) noexcept;

  private:
    hypernet::net::EventLoop &loop_;
    hypernet::net::SessionManager &sm_;

    std::unordered_map<std::string, std::unique_ptr<IConnector>> connectors_;
    std::unordered_map<RequestId, Pending> pending_;
    std::chrono::milliseconds defaultTimeout_{3000};
    RequestId nextRequestId_{1};

    // Dial state table (keeps IFdHandler alive while registered in EventLoop)
    std::unordered_map<DialId, std::shared_ptr<DialState>> dials_;
    DialId nextDialId_{1};
};

} // namespace hypernet::connector
