#pragma once
#include <hyperapp/core/ConnState.hpp>

#include <cstdint>

namespace hyperapp
{
using ScopeId = std::uint32_t;
using TopicId = std::uint32_t;

using AccountId = std::uint64_t;
using PlayerId = std::uint64_t;

struct SessionContext
{
    ConnState state{ConnState::Connected};

    AccountId accountId{0};
    PlayerId playerId{0};

    ScopeId scope{0};
    TopicId topic{0};

    // 확장: room/party/guild/zone/aoi ...
};
} // namespace hyperapp