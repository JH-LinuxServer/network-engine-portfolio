#pragma once

#include <cstddef>

namespace hypernet::protocol {

/// 소유권이 없는 메시지 뷰(view)입니다.
///
/// ===== 수명(lifetime) 규약 (필수) =====
/// - MessageView는 "콜백(onMessage 등)이 실행되는 동안"에만 유효합니다.
/// - 콜백 밖으로 MessageView를 저장/참조하면 UAF(use-after-free) 위험이 있습니다.
///   (예: Session의 RingBuffer가 다음 recv/write로 덮어쓰거나, framer 내부 scratch가 재사용됨)
/// - 콜백 밖으로 보관이 필요하면 "명시적으로 복사"해야 합니다(비용을 코드로 드러낼 것).
class MessageView {
  public:
    MessageView() noexcept = default;
    MessageView(const void *data, std::size_t size) noexcept : data_(data), size_(size) {}

    [[nodiscard]] const void *data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  private:
    const void *data_{nullptr};
    std::size_t size_{0};
};

} // namespace hypernet::protocol
