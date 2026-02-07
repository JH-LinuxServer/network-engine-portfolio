#pragma once

namespace hypernet::util {

/// 단일 타입에 대해 "전역적인" thread_local 인스턴스를 제공하는 간단한 헬퍼입니다.
///
/// - ThreadLocal<T>::instance() 는 스레드마다 독립된 T 객체를 반환합니다.
/// - 하나의 T 타입당 하나의 thread_local 인스턴스가 필요할 때 유용합니다.
///   (동일 타입의 여러 독립 인스턴스가 필요하다면 별도의 TLS 관리 레이어가 필요합니다.)
template <typename T> class ThreadLocal {
  public:
    using value_type = T;

    /// 현재 스레드의 T 인스턴스를 반환합니다.
    /// 처음 호출될 때 한 번 기본 생성자로 초기화됩니다.
    static value_type &instance() {
        thread_local value_type value{};
        return value;
    }

    /// 함수 호출 형태로도 사용할 수 있도록 제공하는 sugar 입니다.
    value_type &operator()() const { return instance(); }
};

} // namespace hypernet::util