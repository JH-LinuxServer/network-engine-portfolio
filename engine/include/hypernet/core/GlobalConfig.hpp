#pragma once

#include <hypernet/EngineConfig.hpp>

#include <cstdint>
#include <string>

namespace hypernet::core
{

// 기존 EngineConfig 내용을 그대로 사용합니다(typedef/alias 형태).
using EngineConfig = hypernet::EngineConfig;

// ExchangeSim(현재: FEP에 접속하는 클라이언트) 전용 설정
struct ExchangeSimConfig
{
    std::string fep_host{"127.0.0.1"};
    std::uint16_t fep_port{9000};

    // 요구사항에 포함된 자리(현재 앱에서 미사용이어도 구조/확장 자리 확보)
    bool autoScope{false};
    int connection_count{1}; // <- 추가
};

// FEP Gateway 전용(향후 Upstream 연결을 위한 자리 확보)
struct FepConfig
{
    std::string upstream_host{};
    std::uint16_t upstream_port{0};
    std::uint16_t worker_threads{0};

    // Scenario routing mode:
    //  - false: local routing (use current worker id)
    //  - true : handoff routing (use client session id -> cross-worker)
    bool handoff_mode{false};
};

// 전체 통합 설정
struct GlobalConfig
{
    EngineConfig engine{};
    ExchangeSimConfig sim{};
    FepConfig fep{};
};

} // namespace hypernet::core
