#pragma once

namespace hypernet::util {

/// 복사와 대입을 금지하기 위한 베이스 클래스입니다.
///
/// - 이 클래스를 상속받는 타입은 자동으로 복사 생성자 / 복사 대입 연산자가 삭제(delete)됩니다.
/// - 이동(move)은 기본적으로 허용되며, 필요하다면 파생 클래스에서 별도로 삭제할 수 있습니다.
class NonCopyable {
  protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;

    NonCopyable(NonCopyable &&) = default;
    NonCopyable &operator=(NonCopyable &&) = default;
};

} // namespace hypernet::util