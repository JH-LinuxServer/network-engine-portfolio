#pragma once

#include <hypernet/EngineConfig.hpp>

namespace hypernet::core
{

/// EngineConfig의 logLevel/logFilePath를 프로세스 전역 Logger에 반영합니다.
void applyLoggingConfig(const hypernet::EngineConfig &cfg);

} // namespace hypernet::core