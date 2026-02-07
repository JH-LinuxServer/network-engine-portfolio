#pragma once

#include <hypernet/core/GlobalConfig.hpp>

namespace hypernet::core
{

class ConfigLoader
{
  public:
    // Apps는 오직 이 한 줄만 호출하면 됩니다.
    static GlobalConfig load(int argc, char **argv);
};

} // namespace hypernet::core