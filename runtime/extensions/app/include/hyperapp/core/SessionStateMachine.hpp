#pragma once

#include <hyperapp/core/ConnState.hpp>
#include <hyperapp/core/SessionRegistry.hpp>
#include <hyperapp/protocol/PacketReader.hpp>

#include <hypernet/SessionHandle.hpp>
#include <hypernet/protocol/Dispatcher.hpp>
#include <hypernet/protocol/MessageView.hpp>

#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace hyperapp
{
class SessionStateMachine
{
  public:
    explicit SessionStateMachine(SessionRegistry &reg) : reg_(reg) {}

    // opcode별 허용 상태 마스크 설정
    void setAllowedStates(std::uint16_t opcode, std::uint32_t mask);

    // [수정] Dispatcher에 “가드된 핸들러” 등록 (최적화 적용: tryGetAllowedContext_ 사용)
    template <typename Fn> void registerGuarded(hypernet::protocol::Dispatcher &dispatcher, std::uint16_t opcode, std::uint32_t allowedMask, Fn &&fn)
    {
        setAllowedStates(opcode, allowedMask);

        dispatcher.registerHandler(opcode,
                                   [this, opcode, fn = std::forward<Fn>(fn)](hypernet::SessionHandle from, const hypernet::protocol::MessageView &body) mutable
                                   {
                                       // [변경] 내부 헬퍼로 조회+검사 한 번에 수행
                                       [[maybe_unused]] SessionContext ctx{};
                                       if (!tryGetAllowedContext_(from.id(), opcode, ctx))
                                           return;
                                       fn(from, body);
                                   });
    }

    // [수정] Dispatcher에 “가드된 핸들러 + ctx” 등록 (최적화 적용: tryGetAllowedContext_ 사용)
    template <typename Fn> void registerGuardedCtx(hypernet::protocol::Dispatcher &dispatcher, std::uint16_t opcode, std::uint32_t allowedMask, Fn &&fn)
    {
        setAllowedStates(opcode, allowedMask);

        dispatcher.registerHandler(opcode,
                                   [this, opcode, fn = std::forward<Fn>(fn)](hypernet::SessionHandle from, const hypernet::protocol::MessageView &body) mutable
                                   {
                                       // [변경] 내부 헬퍼로 조회+검사 후 ctx 획득
                                       SessionContext ctx{};
                                       if (!tryGetAllowedContext_(from.id(), opcode, ctx))
                                           return;

                                       fn(from, body, ctx);
                                   });
    }

    // ============================================================
    // [NEW] 실전용: 상태 가드 + DTO 파싱 + (선택) strict 길이 체크
    // ============================================================
    template <typename PacketType, typename HandlerFn, typename BadFn = std::nullptr_t>
    void registerPacketHandlerCtx(hypernet::protocol::Dispatcher &dispatcher, std::uint16_t opcode, std::uint32_t allowedMask, HandlerFn &&fn, BadFn &&onBadPacket = nullptr, bool strict = true)
    {
        static_assert(std::is_default_constructible_v<PacketType>, "PacketType must be default constructible");
        static_assert(std::is_same_v<decltype(std::declval<PacketType &>().read(std::declval<hyperapp::protocol::PacketReader &>())), bool>, "PacketType must implement: bool read(PacketReader&)");

        registerGuardedCtx(dispatcher, opcode, allowedMask,
                           [fn = std::forward<HandlerFn>(fn), onBadPacket = std::forward<BadFn>(onBadPacket), strict](hypernet::SessionHandle s, const hypernet::protocol::MessageView &raw,
                                                                                                                      const hyperapp::SessionContext &ctx) mutable
                           {
                               hyperapp::protocol::PacketReader r(raw);
                               PacketType pkt{};

                               const bool ok = pkt.read(r) && (!strict || r.expectEnd());
                               if (!ok)
                               {
                                   if constexpr (!std::is_same_v<std::decay_t<BadFn>, std::nullptr_t>)
                                   {
                                       onBadPacket(s, raw, ctx);
                                   }
                                   return;
                               }

                               fn(s, pkt, ctx);
                           });
    }

    // opcode를 PacketType::kOpcode에서 가져오는 오버로드
    template <typename PacketType, typename HandlerFn, typename BadFn = std::nullptr_t>
    void registerPacketHandlerCtx(hypernet::protocol::Dispatcher &dispatcher, std::uint32_t allowedMask, HandlerFn &&fn, BadFn &&onBadPacket = nullptr, bool strict = true)
    {
        registerPacketHandlerCtx<PacketType>(dispatcher, PacketType::kOpcode, allowedMask, std::forward<HandlerFn>(fn), std::forward<BadFn>(onBadPacket), strict);
    }

    bool isAllowed(hypernet::SessionHandle::Id sid, std::uint16_t opcode) const noexcept;
    bool isAllowedCtx(const SessionContext &ctx, std::uint16_t opcode) const noexcept;

  private:
    // [추가] 패치 핵심: 내부 최적화 헬퍼 함수 선언
    [[nodiscard]] bool tryGetAllowedContext_(hypernet::SessionHandle::Id sid, std::uint16_t opcode, SessionContext &out) const noexcept;

    SessionRegistry &reg_;
    std::unordered_map<std::uint16_t, std::uint32_t> allowed_;
};
} // namespace hyperapp