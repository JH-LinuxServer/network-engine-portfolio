#include <hypernet/buffer/RingBuffer.hpp>

#include <algorithm> // std::min
#include <cstring>   // std::memcpy
#include <stdexcept> // std::invalid_argument
#include <sys/uio.h> // struct iovec

namespace hypernet::buffer
{

RingBuffer::RingBuffer(std::size_t capacity) : buffer_(capacity), capacity_(capacity)
{
    if (capacity_ == 0)
    {
        throw std::invalid_argument("RingBuffer capacity must be greater than 0");
    }
}

std::size_t RingBuffer::write(const std::byte *data, std::size_t len) noexcept
{
    if (!data || len == 0)
    {
        return 0;
    }

    // 쓸 수 있는 최대 바이트 수
    const std::size_t toWrite = std::min(len, freeSpace());
    if (toWrite == 0)
    {
        return 0;
    }

    // 버퍼의 끝까지 연속으로 쓸 수 있는 공간
    const std::size_t endSpace = capacity_ - tail_;
    const std::size_t firstPart = std::min(toWrite, endSpace);

    // 첫 번째 조각: tail_ ~ tail_ + firstPart
    std::memcpy(buffer_.data() + tail_, data, firstPart);
    tail_ = (tail_ + firstPart) % capacity_;
    size_ += firstPart;

    // 남은 조각이 있으면 0번 인덱스로 랩어라운드(write)
    const std::size_t remaining = toWrite - firstPart;
    if (remaining > 0)
    {
        std::memcpy(buffer_.data() + tail_, data + firstPart, remaining);
        tail_ = (tail_ + remaining) % capacity_;
        size_ += remaining;
    }

    return toWrite;
}

std::size_t RingBuffer::read(std::byte *dest, std::size_t len) noexcept
{
    if (!dest || len == 0)
    {
        return 0;
    }

    // 읽을 수 있는 최대 바이트 수
    const std::size_t toRead = std::min(len, size_);
    if (toRead == 0)
    {
        return 0;
    }

    // 버퍼의 끝까지 연속으로 읽을 수 있는 데이터
    const std::size_t endData = capacity_ - head_;
    const std::size_t firstPart = std::min(toRead, endData);

    // 첫 번째 조각: head_ ~ head_ + firstPart
    std::memcpy(dest, buffer_.data() + head_, firstPart);
    head_ = (head_ + firstPart) % capacity_;
    size_ -= firstPart;

    // 남은 조각이 있으면 0번 인덱스에서부터 랩어라운드(read)
    const std::size_t remaining = toRead - firstPart;
    if (remaining > 0)
    {
        std::memcpy(dest + firstPart, buffer_.data() + head_, remaining);
        head_ = (head_ + remaining) % capacity_;
        size_ -= remaining;
    }

    return toRead;
}

std::size_t RingBuffer::peek(std::byte *dest, std::size_t len) const noexcept
{
    if (!dest || len == 0)
    {
        return 0;
    }

    const std::size_t toCopy = std::min(len, size_);
    if (toCopy == 0)
    {
        return 0;
    }

    const std::size_t endData = capacity_ - head_;
    const std::size_t firstPart = std::min(toCopy, endData);

    std::memcpy(dest, buffer_.data() + head_, firstPart);

    const std::size_t remaining = toCopy - firstPart;
    if (remaining > 0)
    {
        std::memcpy(dest + firstPart, buffer_.data(), remaining);
    }

    return toCopy;
}

BufferView RingBuffer::peekView(std::size_t maxLen) const noexcept
{
    if (maxLen == 0 || size_ == 0)
    {
        return {};
    }

    // head 에서 끝까지의 "연속된" 데이터 양
    const std::size_t contiguous = std::min(size_, capacity_ - head_);
    const std::size_t viewSize = std::min(maxLen, contiguous);

    if (viewSize == 0)
    {
        return {};
    }

    return BufferView{buffer_.data() + head_, viewSize};
}

BufferView RingBuffer::readView(std::size_t maxLen) noexcept
{
    auto view = peekView(maxLen);
    if (view.empty())
    {
        return view;
    }

    const std::size_t consumed = view.size();
    head_ = (head_ + consumed) % capacity_;
    size_ -= consumed;

    return view;
}

int RingBuffer::peekIov(::iovec out[2], std::size_t maxLen) const noexcept
{
    if (!out || maxLen == 0 || size_ == 0)
        return 0;

    const std::size_t toRead = std::min(maxLen, size_);
    if (toRead == 0)
        return 0;

    const std::size_t endData = capacity_ - head_;
    const std::size_t firstPart = std::min(toRead, endData);
    const std::size_t remaining = toRead - firstPart;

    out[0].iov_base = const_cast<std::byte *>(buffer_.data() + head_);
    out[0].iov_len = firstPart;

    if (remaining > 0)
    {
        out[1].iov_base = const_cast<std::byte *>(buffer_.data()); // wrap to beginning
        out[1].iov_len = remaining;
        return 2;
    }

    out[1].iov_base = nullptr;
    out[1].iov_len = 0;
    return 1;
}
int RingBuffer::writeIov(::iovec out[2], std::size_t maxLen) const noexcept
{
    if (!out || maxLen == 0)
        return 0;

    const std::size_t free = freeSpace();
    if (free == 0)
        return 0;

    const std::size_t toWrite = std::min(maxLen, free);
    if (toWrite == 0)
        return 0;

    const std::size_t endSpace = capacity_ - tail_;
    const std::size_t firstPart = std::min(toWrite, endSpace);
    const std::size_t remaining = toWrite - firstPart;

    out[0].iov_base = const_cast<std::byte *>(buffer_.data() + tail_);
    out[0].iov_len = firstPart;

    if (remaining > 0)
    {
        out[1].iov_base = const_cast<std::byte *>(buffer_.data()); // wrap to beginning
        out[1].iov_len = remaining;
        return 2;
    }

    out[1].iov_base = nullptr;
    out[1].iov_len = 0;
    return 1;
}

void RingBuffer::commitWrite(std::size_t n) noexcept
{
    if (n == 0)
    {
        return;
    }
    // 방어(릴리즈에서도 안전): freeSpace 초과 commit은 버그
    const std::size_t free = freeSpace();
    if (n > free)
    {
        n = free; // 안전 클램프(원하면 assert로 바꿔도 됨)
    }

    tail_ = (tail_ + n) % capacity_;
    size_ += n;
}
void RingBuffer::clear() noexcept
{
    head_ = 0;
    tail_ = 0;
    size_ = 0;
}

} // namespace hypernet::buffer