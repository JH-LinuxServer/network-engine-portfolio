#pragma once

#include <cstdint>

#include <hypernet/protocol/MessageView.hpp>

namespace hypernet::buffer {
class RingBuffer; // forward
}

namespace hypernet::protocol {

/// framer 결과 상태입니다.
/// - NeedMore: 아직 한 프레임이 완성되지 않음(입력 소비 없음)
/// - Framed:   out에 "payload만" 채워서 반환(헤더는 제거/소비됨)
/// - Invalid:  길이/입력 오류(권장 정책: 로그 + session close). 실제 close는 호출자가 수행.
enum class FrameResult : std::uint8_t {
    NeedMore = 0,
    Framed = 1,
    Invalid = 2,
};

/// 스트림 바이트(RingBuffer)에서 "프레임 경계"를 잡아 MessageView로 노출하는 인터페이스입니다.
///
/// 스레딩/소유권:
/// - RingBuffer는 세션 owner worker thread에서만 접근한다는 상위 규약을 전제합니다.
/// - IFramer 자체는 스레드 안전을 보장하지 않습니다(보통 per-session/per-worker로 사용).
class IFramer {
  public:
    virtual ~IFramer() = default;

    /// 입력 버퍼에서 프레임을 0..1개 추출합니다.
    ///
    /// 규약:
    /// - Framed를 반환할 때만 입력을 소비(consumed)합니다.
    /// - NeedMore/Invalid 반환 시 입력 소비는 하지 않는 것을 원칙으로 합니다.
    ///   (단, 구현 버그/불가피한 예외 케이스는 로그로 드러나야 함)
    ///
    /// out 수명:
    /// - out(MessageView)은 "콜백 동안만 유효" 규약을 따른다.
    virtual FrameResult tryFrame(buffer::RingBuffer &in, MessageView &out) = 0;
};

} // namespace hypernet::protocol
