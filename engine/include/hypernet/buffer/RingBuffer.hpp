#pragma once

#include <cstddef> // std::size_t, std::byte
#include <span>
#include <vector>
#include <sys/uio.h> // struct iovec
#include <hypernet/buffer/BufferView.hpp>

namespace hypernet::buffer
{

/// 단일 스레드 환경에서 사용할 고정 크기 바이트 링 버퍼입니다.
///
/// - head/tail 인덱스를 사용하여 선입선출(FIFO) 방식으로 데이터를 저장합니다.
/// - write()는 버퍼에 비어 있는 만큼만 쓰며, 남는 부분은 버립니다(부분 쓰기 가능).
///   -> 전체 쓰기 보장이 필요하면 호출 전에 freeSpace()를 먼저 확인해야 합니다.
/// - read()는 현재 들어있는 데이터까지만 읽어 옵니다.
/// - 단일 스레드용이므로 내부에 락은 없습니다. 다른 스레드에서 동시에 접근하지 않아야 합니다.
class RingBuffer
{
  public:
    /// 주어진 용량(capacity) 만큼의 바이트를 저장할 수 있는 링 버퍼를 생성합니다.
    ///
    /// @param capacity 바이트 단위 용량 (0이면 std::invalid_argument 예외가 발생합니다)
    explicit RingBuffer(std::size_t capacity);

    /// 버퍼의 총 용량(바이트)을 반환합니다.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// 현재 버퍼에 저장되어 읽을 수 있는 바이트 수를 반환합니다.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// size()의 별칭입니다. (읽기 가능한 데이터량)
    [[nodiscard]] std::size_t available() const noexcept { return size_; }

    /// 현재 버퍼에서 추가로 쓸 수 있는 바이트 수를 반환합니다.
    [[nodiscard]] std::size_t freeSpace() const noexcept { return capacity_ - size_; }

    /// 버퍼가 비어 있는지 여부입니다.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// 버퍼가 가득 찼는지 여부입니다.
    [[nodiscard]] bool full() const noexcept { return size_ == capacity_; }

    /// 주어진 데이터에서 최대 len 바이트를 버퍼에 기록합니다.
    ///
    /// - 버퍼에 남은 공간이 부족하면, 쓸 수 있는 만큼만 쓰고 나머지는 무시합니다.
    /// - data 가 nullptr 이거나 len == 0 이면 아무 것도 하지 않고 0을 반환합니다.
    ///
    /// @return 실제로 기록된 바이트 수
    std::size_t write(const std::byte *data, std::size_t len) noexcept;

    /// std::span 버전의 write 헬퍼입니다.
    std::size_t write(std::span<const std::byte> data) noexcept
    {
        return write(data.data(), data.size());
    }

    /// 버퍼에서 최대 len 바이트를 읽어 dest에 저장하고 소비(consumed)합니다.
    ///
    /// - 현재 버퍼에 들어 있는 데이터가 len 보다 적으면, 있는 만큼만 읽습니다.
    /// - dest 가 nullptr 이거나 len == 0 이면 아무 것도 하지 않고 0을 반환합니다.
    ///
    /// @return 실제로 읽은 바이트 수
    std::size_t read(std::byte *dest, std::size_t len) noexcept;

    /// std::span 버전의 read 헬퍼입니다.
    std::size_t read(std::span<std::byte> dest) noexcept { return read(dest.data(), dest.size()); }

    /// 버퍼에서 최대 len 바이트를 dest에 복사하지만, head는 이동하지 않습니다.
    ///
    /// - 즉, 이후 read()를 호출하면 같은 데이터를 다시 읽을 수 있습니다.
    /// - 주로 프로토콜 헤더 파싱 등 "먼저 보고, 나중에 소비" 패턴에 사용합니다.
    ///
    /// @return 실제로 복사한 바이트 수
    std::size_t peek(std::byte *dest, std::size_t len) const noexcept;

    /// std::span 버전의 peek 헬퍼입니다.
    std::size_t peek(std::span<std::byte> dest) const noexcept
    {
        return peek(dest.data(), dest.size());
    }

    /// 현재 head 위치에서 시작하는 "연속된" 데이터 조각을 read-only 뷰로 반환합니다.
    ///
    /// - 최대 maxLen 바이트까지 반환하며, 실제 길이는
    ///   (현재 저장된 데이터 중 head 에서 끝까지의 연속 구간 길이)로 제한됩니다.
    /// - 버퍼가 비어 있거나 연속 구간이 0 바이트인 경우 size()==0 인 뷰를 반환합니다.
    /// - 데이터는 소비되지 않습니다. (peek 계열 API)
    [[nodiscard]] BufferView peekView(std::size_t maxLen) const noexcept;

    /// 현재 head 위치에서 시작하는 "연속된" 데이터 조각을 read-only 뷰로 반환하면서
    /// 해당 바이트 수만큼 버퍼에서 소비(consumed)합니다.
    ///
    /// - 반환되는 뷰의 길이는 peekView(maxLen) 과 동일한 규칙을 따릅니다.
    /// - size()==0 인 뷰가 반환되면 아무 것도 소비되지 않습니다.
    /// - 반환된 BufferView 는 다음 조건을 만족할 때만 안전하게 사용할 수 있습니다:
    ///   - RingBuffer 인스턴스가 뷰보다 오래 살아야 합니다.
    ///   - 뷰를 사용하는 동안에는 해당 구간을 덮어쓰는 write() / clear() 를 호출하지 않아야 합니다.
    [[nodiscard]] BufferView readView(std::size_t maxLen) noexcept;

    /// ====== (NEW) read-side: head 기준으로 최대 maxLen을 1~2 iovec로 노출 (consume 없음)
    /// - writev/sendmsg에 바로 넘기기 위한 용도
    /// - 반환값: iov 개수(0/1/2)
    [[nodiscard]] int peekIov(::iovec out[2], std::size_t maxLen) const noexcept;

    /// ====== (NEW) write-side: tail 기준 freeSpace 내에서 최대 maxLen을 1~2 iovec로 노출
    /// - recvmsg/readv로 "버퍼에 직접 수신"하기 위한 용도
    /// - 주의: 이 함수 호출 후 반드시 commitWrite(n)으로 tail/size를 반영해야 한다.
    /// - 반환값: iov 개수(0/1/2)
    [[nodiscard]] int writeIov(::iovec out[2], std::size_t maxLen) const noexcept;

    /// ====== (NEW) writeIov로 받은 버퍼에 n바이트를 실제로 썼음을 반영
    void commitWrite(std::size_t n) noexcept;

    ///  head/tail이 초기 상태로 돌아갑니다.
    void clear() noexcept;

  private:
    std::vector<std::byte> buffer_; ///< 실제 데이터를 저장하는 순환 배열
    std::size_t capacity_;          ///< 버퍼 용량 (buffer_.size()와 동일)
    std::size_t head_{0};           ///< 다음 read()/peek()가 시작될 위치
    std::size_t tail_{0};           ///< 다음 write()가 기록할 위치
    std::size_t size_{0};           ///< 현재 저장된 데이터 크기 (바이트)
};

} // namespace hypernet::buffer
