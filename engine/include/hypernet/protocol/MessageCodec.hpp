#pragma once

#include <cstddef> // std::byte, std::to_integer
#include <cstdint> // fixed-width ints
#include <cstring> // std::memcpy
#include <span>    // std::span
#include <vector>  // std::vector

namespace hypernet::protocol {

/// MessageCodec: "struct <-> bytes"를 안전하게 구현하기 위한 기본 유틸.
///
/// ===== 엔디안 규약(고정) =====
/// - 모든 정수 필드는 network byte order(big-endian)로 encode/decode 한다.
///
/// ===== 안전성/이식성 규약(중요) =====
/// - struct를 통째로 memcpy 해서 보내는 방식은 금지한다(패딩/정렬/ABI 의존).
/// - 반드시 필드 단위로 명시적으로 write/read 한다.
///
/// 스레딩:
/// - 이 유틸은 순수 값 변환 계층이며 내부 전역 상태가 없다.
/// - 단, ByteWriter/ByteReader 인스턴스 자체는 thread-safe가 아니다(일반 값 객체로 취급).
class ByteWriter {
  public:
    ByteWriter() = default;
    explicit ByteWriter(std::size_t reserveBytes) { buf_.reserve(reserveBytes); }

    void clear() noexcept { buf_.clear(); } // capacity는 유지(재할당 최소화)
    [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
    [[nodiscard]] bool empty() const noexcept { return buf_.empty(); }
    [[nodiscard]] const std::vector<std::byte> &buffer() const noexcept { return buf_; }

    /// 버퍼를 move로 꺼낸다(테스트/빌드 단계에서 편리).
    [[nodiscard]] std::vector<std::byte> release() noexcept { return std::move(buf_); }

    void writeU8(std::uint8_t v) { buf_.push_back(static_cast<std::byte>(v)); }

    void writeU16Be(std::uint16_t v) {
        buf_.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 0) & 0xFF));
    }

    void writeU32Be(std::uint32_t v) {
        buf_.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 0) & 0xFF));
    }

    void writeU64Be(std::uint64_t v) {
        buf_.push_back(static_cast<std::byte>((v >> 56) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 48) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 40) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 32) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<std::byte>((v >> 0) & 0xFF));
    }

    /// 임의 바이트 시퀀스 기록
    /// - data==nullptr 이고 len>0 인 입력은 호출자 버그이므로 무시한다(UB 방지).
    void writeBytes(const void *data, std::size_t len) {
        if (len == 0) {
            return;
        }
        if (!data) {
            return;
        }
        const auto *p = static_cast<const std::byte *>(data);
        buf_.insert(buf_.end(), p, p + len);
    }

  private:
    std::vector<std::byte> buf_;
};

class ByteReader {
  public:
    explicit ByteReader(std::span<const std::byte> data) : data_(data) {}

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] std::size_t offset() const noexcept { return off_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - off_; }

    /// 남은 바이트가 정확히 0인지(“정확히 다 소비했는지”) 체크할 때 사용.
    [[nodiscard]] bool atEnd() const noexcept { return off_ == data_.size(); }

    bool readU8(std::uint8_t &out) noexcept {
        if (!ensure_(1)) {
            return false;
        }
        out = std::to_integer<std::uint8_t>(data_[off_]);
        off_ += 1;
        return true;
    }

    bool readU16Be(std::uint16_t &out) noexcept {
        if (!ensure_(2)) {
            return false;
        }
        const std::uint16_t b0 = std::to_integer<std::uint8_t>(data_[off_ + 0]);
        const std::uint16_t b1 = std::to_integer<std::uint8_t>(data_[off_ + 1]);
        out = static_cast<std::uint16_t>((b0 << 8) | (b1 << 0));
        off_ += 2;
        return true;
    }

    bool readU32Be(std::uint32_t &out) noexcept {
        if (!ensure_(4)) {
            return false;
        }
        const std::uint32_t b0 = std::to_integer<std::uint8_t>(data_[off_ + 0]);
        const std::uint32_t b1 = std::to_integer<std::uint8_t>(data_[off_ + 1]);
        const std::uint32_t b2 = std::to_integer<std::uint8_t>(data_[off_ + 2]);
        const std::uint32_t b3 = std::to_integer<std::uint8_t>(data_[off_ + 3]);
        out = (b0 << 24) | (b1 << 16) | (b2 << 8) | (b3 << 0);
        off_ += 4;
        return true;
    }

    bool readU64Be(std::uint64_t &out) noexcept {
        if (!ensure_(8)) {
            return false;
        }
        const std::uint64_t b0 = std::to_integer<std::uint8_t>(data_[off_ + 0]);
        const std::uint64_t b1 = std::to_integer<std::uint8_t>(data_[off_ + 1]);
        const std::uint64_t b2 = std::to_integer<std::uint8_t>(data_[off_ + 2]);
        const std::uint64_t b3 = std::to_integer<std::uint8_t>(data_[off_ + 3]);
        const std::uint64_t b4 = std::to_integer<std::uint8_t>(data_[off_ + 4]);
        const std::uint64_t b5 = std::to_integer<std::uint8_t>(data_[off_ + 5]);
        const std::uint64_t b6 = std::to_integer<std::uint8_t>(data_[off_ + 6]);
        const std::uint64_t b7 = std::to_integer<std::uint8_t>(data_[off_ + 7]);
        out = (b0 << 56) | (b1 << 48) | (b2 << 40) | (b3 << 32) | (b4 << 24) | (b5 << 16) |
              (b6 << 8) | (b7 << 0);
        off_ += 8;
        return true;
    }

    /// 바이트를 복사해서 읽는다.
    bool readBytes(void *out, std::size_t len) noexcept {
        if (len == 0) {
            return true;
        }
        if (!out) {
            return false;
        }
        if (!ensure_(len)) {
            return false;
        }
        std::memcpy(out, data_.data() + off_, len);
        off_ += len;
        return true;
    }

    /// 복사 없이 view로 읽는다(원본 버퍼 수명은 호출자가 관리).
    bool readBytesView(std::size_t len, std::span<const std::byte> &out) noexcept {
        if (!ensure_(len)) {
            return false;
        }
        out = data_.subspan(off_, len);
        off_ += len;
        return true;
    }

    bool skip(std::size_t len) noexcept {
        if (!ensure_(len)) {
            return false;
        }
        off_ += len;
        return true;
    }

  private:
    std::span<const std::byte> data_{};
    std::size_t off_{0};

    [[nodiscard]] bool ensure_(std::size_t n) const noexcept { return (off_ + n) <= data_.size(); }
};

} // namespace hypernet::protocol
