#pragma once

#include <cstddef> // std::size_t, std::byte
#include <span>

namespace hypernet::buffer {

/// 소유권이 없는(read-only) 바이트 시퀀스 뷰입니다.
///
/// - std::span<const std::byte> 와 유사하게, "시작 포인터 + 길이"만을 보유합니다.
/// - 실제 메모리(원본 버퍼)의 수명은 BufferView 를 생성한 쪽에서 관리해야 합니다.
/// - 엔진의 프로토콜 파싱 계층에서는 메시지 헤더/바디를 복사 없이 가리키는 용도로 사용하게 됩니다.
class BufferView {
  public:
    using value_type = std::byte;
    using const_pointer = const std::byte *;
    using const_iterator = const std::byte *;

    /// 빈 뷰(Default 생성자)입니다. data()==nullptr, size()==0 입니다.
    BufferView() noexcept = default;

    /// 임의의 메모리 블록에서 뷰를 생성합니다.
    ///
    /// @param data 바이트 버퍼의 시작 주소 (nullptr 허용, 이 경우 size 는 반드시 0 이어야 합니다)
    /// @param size 바이트 수
    BufferView(const void *data, std::size_t size) noexcept
        : data_(static_cast<const std::byte *>(data)), size_(size) {}

    /// std::span<const std::byte> 로부터 뷰를 생성하는 헬퍼입니다.
    static BufferView fromSpan(std::span<const std::byte> span) noexcept {
        return BufferView(span.data(), span.size());
    }

    /// 뷰의 시작 주소를 반환합니다. (소유권 없음)
    [[nodiscard]] const std::byte *data() const noexcept { return data_; }

    /// 뷰가 가리키는 바이트 수를 반환합니다.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// size() == 0 인지 여부를 반환합니다.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// 범위 기반 for 문을 위한 iterator 들입니다.
    [[nodiscard]] const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] const_iterator end() const noexcept { return data_ + size_; }

    /// std::span 형태로 다시 래핑하여 사용하고 싶을 때를 위한 헬퍼입니다.
    [[nodiscard]] std::span<const std::byte> asSpan() const noexcept { return {data_, size_}; }

  private:
    const std::byte *data_{nullptr};
    std::size_t size_{0};
};

} // namespace hypernet::buffer
