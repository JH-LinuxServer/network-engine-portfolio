#include <hypernet/core/ConfigLoader.hpp>

#include <hypernet/core/Logger.hpp>
#include <hypernet/EngineConfig.hpp>

#include "libs/toml.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace hypernet;
using namespace hypernet::core;

// -----------------------------------------------------------------------------
// Helper Functions (Validation & Parsing)
// -----------------------------------------------------------------------------

static void printUsage(const char *argv0)
{
    std::string exe = "app";
    if (argv0 && *argv0)
    {
        try
        {
            exe = std::filesystem::path(argv0).filename().string();
        }
        catch (...)
        {
            exe = std::string(argv0);
        }
    }
    std::cout << "Usage: " << exe << " --config <path.toml>\n";
}

static LogLevel parseLogLevel(std::string_view s)
{
    std::string v(s);
    for (auto &c : v)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (v == "trace")
        return LogLevel::Trace;
    if (v == "debug")
        return LogLevel::Debug;
    if (v == "info")
        return LogLevel::Info;
    if (v == "warn" || v == "warning")
        return LogLevel::Warn;
    if (v == "error")
        return LogLevel::Error;
    if (v == "fatal")
        return LogLevel::Fatal;

    throw std::invalid_argument("Invalid log_level: " + std::string(s));
}

static std::optional<std::string> scanCliForConfigPath(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string_view a = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (a == "--config" || a == "-c")
        {
            if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1])
                throw std::runtime_error("--config requires a path");
            return std::string(argv[i + 1]);
        }
    }
    return std::nullopt;
}

// [Strict Mode] Helper: Must check range safely
static std::uint16_t checkedPortFromI64(std::int64_t v, const char *key)
{
    if (v < 0 || v > 65535)
        throw std::invalid_argument(std::string(key) + " out of range (0..65535): " + std::to_string(v));
    return static_cast<std::uint16_t>(v);
}

static unsigned int checkedUIntFromI64(std::int64_t v, const char *key)
{
    if (v < 0 || v > static_cast<std::int64_t>(std::numeric_limits<unsigned int>::max()))
        throw std::invalid_argument(std::string(key) + " out of range: " + std::to_string(v));
    return static_cast<unsigned int>(v);
}

static std::size_t checkedSizeFromI64(std::int64_t v, const char *key)
{
    if (v < 0)
        throw std::invalid_argument(std::string(key) + " must be non-negative: " + std::to_string(v));
    return static_cast<std::size_t>(v);
}

// [Strict Mode] Helper: Required Table
static const toml::table &requireTable(const toml::table &root, const char *name)
{
    const auto *t = root[name].as_table();
    if (!t)
        throw std::runtime_error(std::string("Missing required [") + name + "] section");
    return *t;
}

// [Strict Mode] Helper: Direct Key Access (No Fallback)
static toml::node_view<const toml::node> engineKey(const toml::table &engine, std::string_view key)
{
    return engine[key];
}

// -----------------------------------------------------------------------------
// Main Parsing Logic
// -----------------------------------------------------------------------------

static void applyEngineToml(GlobalConfig &cfg, const toml::table &root)
{
    // [Strict] 엔진은 무조건 [engine] 섹션 필수
    const toml::table &engine = requireTable(root, "engine");

    if (auto v = engineKey(engine, "listen_port").value<std::int64_t>())
        cfg.engine.listenPort = checkedPortFromI64(*v, "listen_port");

    // [Strict] metrics_http_port 제거 -> metrics_port만 허용
    if (auto v = engineKey(engine, "metrics_port").value<std::int64_t>())
        cfg.engine.metricsHttpPort = checkedPortFromI64(*v, "metrics_port");

    // [Strict] workers 제거 -> worker_threads만 허용
    if (auto v = engineKey(engine, "worker_threads").value<std::int64_t>())
        cfg.engine.workerThreads = checkedUIntFromI64(*v, "worker_threads");

    if (auto s = engineKey(engine, "log_level").value<std::string>())
        cfg.engine.logLevel = parseLogLevel(*s);

    if (auto s = engineKey(engine, "listen_address").value<std::string>())
        cfg.engine.listenAddress = *s;

    if (auto v = engineKey(engine, "listen_backlog").value<std::int64_t>())
        cfg.engine.listenBacklog = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "listen_backlog"));

    if (auto b = engineKey(engine, "reuse_port").value<bool>())
        cfg.engine.reusePort = *b;
    else if (auto i = engineKey(engine, "reuse_port").value<std::int64_t>())
        cfg.engine.reusePort = (*i != 0);

    if (auto s = engineKey(engine, "log_file_path").value<std::string>())
        cfg.engine.logFilePath = *s;

    if (auto s = engineKey(engine, "metrics_http_address").value<std::string>())
        cfg.engine.metricsHttpAddress = *s;

    // Tuning Params
    if (auto v = engineKey(engine, "idle_timeout_ms").value<std::int64_t>())
        cfg.engine.idleTimeoutMs = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "idle_timeout_ms"));
    if (auto v = engineKey(engine, "heartbeat_interval_ms").value<std::int64_t>())
        cfg.engine.heartbeatIntervalMs = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "heartbeat_interval_ms"));

    if (auto v = engineKey(engine, "shutdown_drain_timeout_ms").value<std::int64_t>())
        cfg.engine.shutdownDrainTimeoutMs = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "shutdown_drain_timeout_ms"));
    if (auto v = engineKey(engine, "shutdown_poll_interval_ms").value<std::int64_t>())
        cfg.engine.shutdownPollIntervalMs = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "shutdown_poll_interval_ms"));

    if (auto v = engineKey(engine, "tick_resolution_ms").value<std::int64_t>())
        cfg.engine.tickResolutionMs = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "tick_resolution_ms"));
    if (auto v = engineKey(engine, "timer_slots").value<std::int64_t>())
        cfg.engine.timerSlots = checkedSizeFromI64(*v, "timer_slots");
    if (auto v = engineKey(engine, "max_epoll_events").value<std::int64_t>())
        cfg.engine.maxEpollEvents = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "max_epoll_events"));

    if (auto v = engineKey(engine, "buffer_block_size").value<std::int64_t>())
        cfg.engine.bufferBlockSize = checkedSizeFromI64(*v, "buffer_block_size");
    if (auto v = engineKey(engine, "buffer_block_count").value<std::int64_t>())
        cfg.engine.bufferBlockCount = checkedSizeFromI64(*v, "buffer_block_count");
    if (auto v = engineKey(engine, "recv_ring_capacity").value<std::int64_t>())
        cfg.engine.recvRingCapacity = checkedSizeFromI64(*v, "recv_ring_capacity");
    if (auto v = engineKey(engine, "send_ring_capacity").value<std::int64_t>())
        cfg.engine.sendRingCapacity = checkedSizeFromI64(*v, "send_ring_capacity");

    if (auto v = engineKey(engine, "max_payload_len").value<std::int64_t>())
        cfg.engine.maxPayloadLen = static_cast<std::uint32_t>(checkedUIntFromI64(*v, "max_payload_len"));
}

static void applyAppToml(GlobalConfig &cfg, const toml::table &root)
{
    if (auto *app = root["app"].as_table())
    {
        // Simulator (loadgen/client) config
        const toml::table *sim = nullptr;
        if (auto *t = (*app)["loadgen"].as_table())
            sim = t;
        else if (auto *t2 = (*app)["exchange_sim"].as_table())
            sim = t2;
        else if (auto *t3 = (*app)["client"].as_table())
            sim = t3;

        if (sim)
        {
            if (auto s = (*sim)["fep_host"].value<std::string>())
                cfg.sim.fep_host = *s;
            else if (auto s2 = (*sim)["upstream_host"].value<std::string>())
                cfg.sim.fep_host = *s2;

            if (auto v = (*sim)["fep_port"].value<std::int64_t>())
                cfg.sim.fep_port = checkedPortFromI64(*v, "fep_port");
            else if (auto v2 = (*sim)["upstream_port"].value<std::int64_t>())
                cfg.sim.fep_port = checkedPortFromI64(*v2, "upstream_port");

            // [Strict] autoScope 제거 -> auto_scope만 허용
            // [FIXED] 패치에서 누락되었던 auto_scope 복구 완료
            if (auto b = (*sim)["auto_scope"].value<bool>())
                cfg.sim.autoScope = *b;
            if (auto v = (*sim)["connection_count"].value<std::int64_t>())
                cfg.sim.connection_count = static_cast<int>(*v);
        }

        // FEP gateway config
        auto *fep = (*app)["fep_gateway"].as_table();
        if (fep)
        {
            if (auto s = (*fep)["upstream_host"].value<std::string>())
                cfg.fep.upstream_host = *s;
            if (auto v = (*fep)["upstream_port"].value<std::int64_t>())
                cfg.fep.upstream_port = checkedPortFromI64(*v, "upstream_port");

            // [NEW] scenario routing toggle (S2/S3)
            if (auto b = (*fep)["handoff_mode"].value<bool>())
                cfg.fep.handoff_mode = *b;

            // mirror engine worker threads into app-level config for convenience
            cfg.fep.worker_threads = cfg.engine.workerThreads;
        }
    }
}

static void validateFailFast(const GlobalConfig &cfg, const toml::table &root)
{
    // 간단한 포트 범위 검증
    if (cfg.engine.listenPort > 65535)
        throw std::runtime_error("listen_port out of range");

    // TOML에 [app] 섹션이 있는지 확인
    bool hasApp = (root["app"].as_table() != nullptr);
    if (!hasApp)
    {
        // 앱 섹션이 없어도 엔진 검증은 수행
        validateEngineConfig(cfg.engine);
        return;
    }

    const auto *app = root["app"].as_table();
    // [수정] client 섹션도 시뮬레이터 후보로 포함하여 검증 누락 방지
    bool wantSim = (*app)["loadgen"].is_table() || (*app)["exchange_sim"].is_table() || (*app)["client"].is_table();

    if (wantSim)
    {
        if (cfg.sim.fep_host.empty())
            throw std::runtime_error("Config Error: Loadgen requires 'fep_host'");
        if (cfg.sim.fep_port == 0)
            throw std::runtime_error("Config Error: Loadgen requires 'fep_port'");
    }

    // [추가] 엔진 설정 전체 유효성 검사 (EngineConfig.hpp 내 기능 활용)
    validateEngineConfig(cfg.engine);
}

} // namespace

namespace hypernet::core
{

GlobalConfig ConfigLoader::load(int argc, char **argv)
{
    // Help Check
    for (int i = 1; i < argc; ++i)
    {
        std::string_view a = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (a == "--help" || a == "-h")
        {
            printUsage((argc > 0) ? argv[0] : nullptr);
            std::exit(0);
        }
    }

    // 1. Path Injection
    auto configOpt = scanCliForConfigPath(argc, argv);
    if (!configOpt.has_value())
    {
        printUsage((argc > 0) ? argv[0] : nullptr);
        throw std::runtime_error("Missing required argument: --config <path.toml>");
    }

    std::string configPath = *configOpt;
    if (!std::filesystem::exists(configPath))
    {
        throw std::runtime_error("Config file not found: " + configPath);
    }

    // 2. Parse TOML
    toml::table root;
    try
    {
        root = toml::parse_file(configPath);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("TOML Parse Error: " + std::string(e.what()));
    }

    // 3. Apply Settings (Strict)
    GlobalConfig cfg{};
    applyEngineToml(cfg, root);
    applyAppToml(cfg, root);

    // 4. Validate
    validateFailFast(cfg, root);

    std::cout << "[ConfigLoader] Successfully loaded: " << configPath << "\n";
    return cfg;
}

} // namespace hypernet::core
