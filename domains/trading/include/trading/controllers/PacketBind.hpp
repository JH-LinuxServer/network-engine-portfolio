#pragma once

#include <hyperapp/core/ConnState.hpp>
#include <hyperapp/core/AppRuntime.hpp>
#include <hyperapp/core/SessionContext.hpp>

#include <hypernet/SessionHandle.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/protocol/Dispatcher.hpp>
#include <hypernet/protocol/MessageView.hpp>

#include <trading/protocol/OpcodePolicy.hpp>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

// Legacy wrappers
#define BIND_PACKET(PacketType, HandlerFunc) trading::bind::bindPackets(dispatcher, runtime, self).allowStates(hyperapp::ConnState::Connected).on<PacketType>(&HandlerFunc, nullptr, true)
#define BIND_PACKET_WITH_STATE(PacketType, State, HandlerFunc) trading::bind::bindPackets(dispatcher, runtime, self).allowStates(State).on<PacketType>(&HandlerFunc, nullptr, true)
#define BIND_PACKET_WITH_STATES(PacketType, HandlerFunc, ...) trading::bind::bindPackets(dispatcher, runtime, self).allowStates(__VA_ARGS__).on<PacketType>(&HandlerFunc, nullptr, true)

namespace trading::bind
{

// [PR-4] Fail-Fast Helper
#if defined(FEP_BIND_FAILFAST) && FEP_BIND_FAILFAST
inline constexpr bool kBindFailFast = true;
#else
inline constexpr bool kBindFailFast = false;
#endif

inline void reportViolation(const char *reason, std::uint16_t opcode = 0) noexcept
{
    SLOG_ERROR("PacketBind", reason, "Violation detected. opcode={} failfast={}", opcode, kBindFailFast);
    if constexpr (kBindFailFast)
    {
        std::abort();
    }
}

// -----------------------------------------------------------
// Helper: Opcode Policy Check
// -----------------------------------------------------------
inline void enforceTradingOpcodePolicyOrDie(std::uint16_t opcode) noexcept
{
    if (trading::protocol::isValidTradingOpcode(opcode))
        return;
    reportViolation("InvalidOpcode", opcode);
}

// -----------------------------------------------------------
// Helper: State Bitmask
// -----------------------------------------------------------
template <typename... S> constexpr std::uint32_t states(S... st) noexcept
{
    static_assert(sizeof...(st) > 0, "states(...) needs at least 1 ConnState");
    return (hyperapp::stateBit(st) | ...);
}

// -----------------------------------------------------------
// Helper: Default Bad Packet Handler
// -----------------------------------------------------------
inline void defaultBadPacket(std::uint16_t opcode, hypernet::SessionHandle s, const hypernet::protocol::MessageView &raw, const hyperapp::SessionContext &ctx) noexcept
{
    SLOG_WARN("PacketBind", "BadPacket", "opcode={} sid={} bytes={} state={}", opcode, s.id(), raw.size(), static_cast<int>(ctx.state));
}

namespace detail
{
// [기존 코드 유지] Trait: Supported packet handler signatures
template <typename Handler, typename Self, typename Packet>
inline constexpr bool is_supported_handler_v =
    std::is_invocable_v<Handler, Self &, hyperapp::AppRuntime &, hypernet::SessionHandle, const Packet &, const hyperapp::SessionContext &> ||
    std::is_invocable_v<Handler, Self &, hypernet::SessionHandle, const Packet &, const hyperapp::SessionContext &> ||
    std::is_invocable_v<Handler, Self &, hyperapp::AppRuntime &, hypernet::SessionHandle, const Packet &> || std::is_invocable_v<Handler, Self &, hypernet::SessionHandle, const Packet &>;

// [기존 코드 유지] Invoke Helper
template <typename Handler, typename Self, typename Packet>
constexpr void invoke_handler(Self &self, hyperapp::AppRuntime &rt, hypernet::SessionHandle s, const Packet &pkt, const hyperapp::SessionContext &ctx, Handler &&h)
{
    if constexpr (std::is_invocable_v<Handler, Self &, hyperapp::AppRuntime &, hypernet::SessionHandle, const Packet &, const hyperapp::SessionContext &>)
        std::invoke(std::forward<Handler>(h), self, rt, s, pkt, ctx);
    else if constexpr (std::is_invocable_v<Handler, Self &, hypernet::SessionHandle, const Packet &, const hyperapp::SessionContext &>)
        std::invoke(std::forward<Handler>(h), self, s, pkt, ctx);
    else if constexpr (std::is_invocable_v<Handler, Self &, hyperapp::AppRuntime &, hypernet::SessionHandle, const Packet &>)
        std::invoke(std::forward<Handler>(h), self, rt, s, pkt);
    else
        std::invoke(std::forward<Handler>(h), self, s, pkt);
}

// [기존 코드 유지] Bad Invoke Helper
template <typename BadHandler, typename Self>
constexpr void invoke_bad(Self &self, hyperapp::AppRuntime &, std::uint16_t opcode, hypernet::SessionHandle s, const hypernet::protocol::MessageView &raw, const hyperapp::SessionContext &ctx,
                          BadHandler &&bh)
{
    if constexpr (std::is_same_v<std::decay_t<BadHandler>, std::nullptr_t>)
        defaultBadPacket(opcode, s, raw, ctx);
    else
    {
        if constexpr (std::is_invocable_v<BadHandler, Self &, hypernet::SessionHandle, const hypernet::protocol::MessageView &, const hyperapp::SessionContext &, std::uint16_t>)
            std::invoke(std::forward<BadHandler>(bh), self, s, raw, ctx, opcode);
        else
            defaultBadPacket(opcode, s, raw, ctx);
    }
}
} // namespace detail

// -----------------------------------------------------------
// 1. Main Registration Function
// -----------------------------------------------------------
template <typename PacketType, typename Self, typename Handler, typename BadHandler = std::nullptr_t>
void registerPacketCtx(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime, std::uint16_t opcode, std::uint32_t allowedMask, const std::shared_ptr<Self> &self, Handler &&handler,
                       BadHandler &&bad = nullptr, bool strict = true)
{
    enforceTradingOpcodePolicyOrDie(opcode);

    if (allowedMask == 0)
    {
        reportViolation("EmptyAllowedMask", opcode); // [PR-4]
    }

    runtime.registerPacketHandlerCtx<PacketType>(
        dispatcher, opcode, allowedMask, [self, &runtime, handler = std::forward<Handler>(handler)](hypernet::SessionHandle s, const PacketType &pkt, const hyperapp::SessionContext &ctx) mutable
        { detail::invoke_handler(*self, runtime, s, pkt, ctx, handler); },
        [self, &runtime, opcode, bad = std::forward<BadHandler>(bad)](hypernet::SessionHandle s, const hypernet::protocol::MessageView &raw, const hyperapp::SessionContext &ctx) mutable
        { detail::invoke_bad(*self, runtime, opcode, s, raw, ctx, bad); }, strict);
}

// -----------------------------------------------------------
// 2. Opcode Inference Overload
// -----------------------------------------------------------
template <typename PacketType, typename Self, typename Handler, typename BadHandler = std::nullptr_t>
void registerPacketCtx(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime, std::uint32_t allowedMask, const std::shared_ptr<Self> &self, Handler &&handler,
                       BadHandler &&bad = nullptr, bool strict = true)
{
    static_assert(trading::protocol::isValidTradingOpcode(PacketType::kOpcode), "OpcodePolicy violation");
    registerPacketCtx<PacketType>(dispatcher, runtime, PacketType::kOpcode, allowedMask, self, std::forward<Handler>(handler), std::forward<BadHandler>(bad), strict);
}

// -----------------------------------------------------------
// Type-safe Binder API
// -----------------------------------------------------------
template <typename Self> class PacketBinder final
{
  public:
    PacketBinder(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime, std::shared_ptr<Self> self) : dispatcher_(dispatcher), runtime_(runtime), self_(std::move(self)) {}

    class Allowed final
    {
      public:
        Allowed(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime, std::shared_ptr<Self> self, std::uint32_t allowedMask)
            : dispatcher_(dispatcher), runtime_(runtime), self_(std::move(self)), allowedMask_(allowedMask)
        {
        }

        // [PR-4] Move Constructor: 소유권 이전 시 기존 객체의 bound_ 검사를 무력화 (중복 로그 방지)
        Allowed(Allowed &&other) noexcept : dispatcher_(other.dispatcher_), runtime_(other.runtime_), self_(std::move(other.self_)), allowedMask_(other.allowedMask_), bound_(other.bound_)
        {
            other.bound_ = true; // Moved-from object is considered "handled"
        }

        // [PR-4] Destructor: 바인딩이 수행되지 않은 채 소멸되면 누락으로 간주
        ~Allowed()
        {
            if (!bound_)
            {
                reportViolation("MissingBinding");
            }
        }

        template <typename PacketType, typename Handler, typename BadHandler = std::nullptr_t> void on(Handler &&handler, BadHandler &&bad = nullptr, bool strict = true)
        {
            // [PR-4] Mark as bound
            bound_ = true;

            static_assert(detail::is_supported_handler_v<std::decay_t<Handler>, Self, PacketType>, "Unsupported handler signature. Please use (Self&, Runtime&, Session, Pkt, Ctx).");

            registerPacketCtx<PacketType>(dispatcher_, runtime_, allowedMask_, self_, std::forward<Handler>(handler), std::forward<BadHandler>(bad), strict);
        }

      private:
        hypernet::protocol::Dispatcher &dispatcher_;
        hyperapp::AppRuntime &runtime_;
        std::shared_ptr<Self> self_;
        std::uint32_t allowedMask_;
        bool bound_ = false; // [PR-4] Track usage
    };

    template <typename... States> [[nodiscard]] Allowed allowStates(States... st) const { return Allowed(dispatcher_, runtime_, self_, states(st...)); }
    [[nodiscard]] Allowed allowMask(std::uint32_t allowedMask) const { return Allowed(dispatcher_, runtime_, self_, allowedMask); }

  private:
    hypernet::protocol::Dispatcher &dispatcher_;
    hyperapp::AppRuntime &runtime_;
    std::shared_ptr<Self> self_;
};

template <typename Self> [[nodiscard]] inline PacketBinder<Self> bindPackets(hypernet::protocol::Dispatcher &dispatcher, hyperapp::AppRuntime &runtime, std::shared_ptr<Self> self)
{
    return PacketBinder<Self>(dispatcher, runtime, self);
}

} // namespace trading::bind