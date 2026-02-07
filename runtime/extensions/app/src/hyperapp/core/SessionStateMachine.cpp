#include <hyperapp/core/SessionStateMachine.hpp>

namespace hyperapp
{
void SessionStateMachine::setAllowedStates(std::uint16_t opcode, std::uint32_t mask)
{
    allowed_[opcode] = mask;
}

// [추가] 내부 최적화 헬퍼 구현
// - Registry의 bool 반환 버전 tryGetContext 사용
// - 조회와 상태 검사를 한 곳에서 처리
bool SessionStateMachine::tryGetAllowedContext_(hypernet::SessionHandle::Id sid, std::uint16_t opcode, SessionContext &out) const noexcept
{
    auto it = allowed_.find(opcode);
    if (it == allowed_.end())
        return false;

    // SessionRegistry에 새로 추가된 API 호출 (SessionContext& out 채우기)
    if (!reg_.tryGetContext(sid, out))
        return false;

    return (it->second & stateBit(out.state)) != 0;
}

// [수정] 헬퍼를 재사용하여 코드 중복 제거
bool SessionStateMachine::isAllowed(hypernet::SessionHandle::Id sid, std::uint16_t opcode) const noexcept
{
    SessionContext ctx{};
    return tryGetAllowedContext_(sid, opcode, ctx);
}

bool SessionStateMachine::isAllowedCtx(const SessionContext &ctx, std::uint16_t opcode) const noexcept
{
    auto it = allowed_.find(opcode);
    if (it == allowed_.end())
        return false;

    return (it->second & stateBit(ctx.state)) != 0;
}
} // namespace hyperapp