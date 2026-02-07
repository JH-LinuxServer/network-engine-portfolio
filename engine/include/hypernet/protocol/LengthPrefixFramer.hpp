#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <hypernet/buffer/RingBuffer.hpp>
#include <hypernet/core/Logger.hpp>
#include <hypernet/core/ThreadContext.hpp>
#include <hypernet/protocol/IFramer.hpp>
#include <hypernet/protocol/Dispatcher.hpp>
#include <hypernet/protocol/MessageView.hpp>
#include <hypernet/protocol/Endian.hpp>

namespace hypernet::protocol
{

/**
 * @brief MessageHeader::kLengthFieldBytes 크기의 길이 헤더를 사용하는 프레이머입니다.
 * * [Wire Format]
 * - [0..3] : Payload Length (u32, Big-Endian) - Opcode(2) + Body(N)의 합계
 * - [4..]  : Payload (Opcode + Body)
 * * [메모리 정책]
 * - RingBuffer 내 데이터가 연속적이면 readView()를 통해 제로-카피(Zero-copy)로 추출합니다.
 * - 링버퍼 Wrap-around 발생 시에만 내부 scratch 버퍼로 복사하여 연속성을 확보합니다.
 */
class LengthPrefixFramer final : public IFramer
{
  public:
    static constexpr std::uint32_t kDefaultMaxPayloadLen = 1024U * 1024U; // 1 MiB

    explicit LengthPrefixFramer(std::uint32_t maxPayloadLen = kDefaultMaxPayloadLen)
        : maxPayloadLen_(maxPayloadLen)
    {
        // 핫패스 성능 최적화: 설정된 최대 페이로드 크기만큼 미리 메모리 예약
        scratch_.reserve(static_cast<std::size_t>(maxPayloadLen_));
    }

    [[nodiscard]] std::uint32_t maxPayloadLen() const noexcept { return maxPayloadLen_; }
    [[nodiscard]] const char *lastErrorReason() const noexcept { return lastErrorReason_; }

    FrameResult tryFrame(buffer::RingBuffer &in, MessageView &out) override
    {
        lastErrorReason_ = nullptr;
        out = MessageView{};

        // Framing SSOT: MessageHeader에 정의된 필드 크기를 사용 (Magic Number 제거)
        constexpr std::size_t kHeaderSize = MessageHeader::kLengthFieldBytes;

        // 1. 헤더 크기만큼 데이터가 들어왔는지 확인
        if (in.available() < kHeaderSize)
        {
            return FrameResult::NeedMore;
        }

        // 2. 헤더 피크 (Peek): 버퍼에서 제거하지 않고 길이 정보만 확인
        std::array<std::byte, kHeaderSize> hdr{};
        const std::size_t copied = in.peek(hdr.data(), kHeaderSize);
        if (copied != kHeaderSize)
        {
            lastErrorReason_ = "ringbuffer_peek_short";
            return FrameResult::Invalid;
        }

        // 네트워크 바이트 순서(Big-Endian)를 호스트 바이트 순서로 변환
        const std::uint32_t payloadLen32 = hypernet::protocol::loadU32Be(hdr.data());
        const std::size_t payloadLen = static_cast<std::size_t>(payloadLen32);

        // 3. 보안 정책 검증 (DoS 방지)
        if (payloadLen32 > maxPayloadLen_)
        {
            lastErrorReason_ = "payload_len_exceeds_max";
            SLOG_WARN("LengthPrefixFramer", "InvalidPayloadLen", "len={} max={}", payloadLen32,
                      maxPayloadLen_);
            return FrameResult::Invalid;
        }

        // 4. 오버플로우 및 버퍼 용량 검증
        if (payloadLen > (std::numeric_limits<std::size_t>::max() - kHeaderSize))
        {
            lastErrorReason_ = "size_overflow";
            return FrameResult::Invalid;
        }

        const std::size_t totalFrameBytes = kHeaderSize + payloadLen;

        // 현재 링버퍼 전체 용량보다 큰 프레임은 처리 불가
        if (totalFrameBytes > in.capacity())
        {
            lastErrorReason_ = "frame_exceeds_ring_capacity";
            return FrameResult::Invalid;
        }

        // 5. 전체 프레임(헤더 + 페이로드)이 수신될 때까지 대기
        if (in.available() < totalFrameBytes)
        {
            return FrameResult::NeedMore;
        }

        // 6. 헤더 소비: 실제 데이터 추출을 위해 헤더 4바이트를 버퍼에서 읽어 제거
        std::array<std::byte, kHeaderSize> tmp{};
        const std::size_t headerRead = in.read(tmp.data(), kHeaderSize);
        if (headerRead != kHeaderSize)
        {
            lastErrorReason_ = "ringbuffer_read_header_failed";
            return FrameResult::Invalid;
        }

        // 페이로드 길이가 0인 경우(빈 메시지) 즉시 처리 완료
        if (payloadLen == 0)
        {
            out = MessageView{nullptr, 0};
            return FrameResult::Framed;
        }

        // 7. 데이터 추출 (Zero-copy 경로 시도)
        // [중요] 사용자 엔진 RingBuffer API(peekView)를 사용하여 연속성 확인
        const auto contiguous = in.peekView(payloadLen);
        if (contiguous.size() == payloadLen)
        {
            // 데이터가 연속적이므로 readView를 통해 복사 없이 포인터만 획득 후 소비
            const auto payloadView = in.readView(payloadLen);
            out = MessageView{payloadView.data(), payloadView.size()};
            return FrameResult::Framed;
        }

        // 8. 데이터 추출 (Fallback: Copy 경로)
        // 링버퍼 Wrap-around 발생 시에만 scratch 버퍼에 모아서 연속성 확보
        scratch_.resize(payloadLen);
        const std::size_t payloadRead = in.read(scratch_.data(), payloadLen);
        if (payloadRead != payloadLen)
        {
            lastErrorReason_ = "ringbuffer_read_payload_short";
            return FrameResult::Invalid;
        }

        out = MessageView{scratch_.data(), payloadLen};
        return FrameResult::Framed;
    }

  private:
    std::uint32_t maxPayloadLen_{kDefaultMaxPayloadLen};
    std::vector<std::byte> scratch_;
    const char *lastErrorReason_{nullptr};
};

} // namespace hypernet::protocol