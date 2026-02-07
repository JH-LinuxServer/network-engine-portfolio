#pragma once

#include <hypernet/protocol/Endian.hpp>
#include <hypernet/protocol/MessageView.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hyperapp::protocol
{
/// Explicit serialization reader for MessageView.
/// - IMPORTANT: MessageView data (and any string_view you read from it) is valid ONLY during the
/// callback.
class PacketReader
{
  public:
    PacketReader() noexcept = default;

    explicit PacketReader(const hypernet::protocol::MessageView &v) noexcept { reset(v); }

    void reset(const hypernet::protocol::MessageView &v) noexcept
    {
        if (!v.data() || v.size() == 0)
        {
            data_ = empty_();
            size_ = 0;
            pos_ = 0;
            return;
        }
        data_ = static_cast<const std::uint8_t *>(v.data());
        size_ = v.size();
        pos_ = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return (pos_ <= size_) ? (size_ - pos_) : 0; }
    [[nodiscard]] bool eof() const noexcept { return pos_ == size_; }
    [[nodiscard]] bool expectEnd() const noexcept { return eof(); }

    bool skip(std::size_t n) noexcept
    {
        if (remaining() < n)
            return false;
        pos_ += n;
        return true;
    }

    bool readU8(std::uint8_t &out) noexcept
    {
        if (remaining() < 1)
            return false;
        out = data_[pos_];
        pos_ += 1;
        return true;
    }

    bool readU16Be(std::uint16_t &out) noexcept
    {
        if (remaining() < 2)
            return false;
        out = hypernet::protocol::loadU16Be(data_ + pos_);
        pos_ += 2;
        return true;
    }

    bool readU32Be(std::uint32_t &out) noexcept
    {
        if (remaining() < 4)
            return false;
        out = hypernet::protocol::loadU32Be(data_ + pos_);
        pos_ += 4;
        return true;
    }

    bool readU64Be(std::uint64_t &out) noexcept
    {
        if (remaining() < 8)
            return false;
        out = hypernet::protocol::loadU64Be(data_ + pos_);
        pos_ += 8;
        return true;
    }

    /// Returns a view into the underlying buffer (lifetime == callback lifetime)
    bool readBytes(std::string_view &out, std::size_t n) noexcept
    {
        if (remaining() < n)
            return false;
        out = std::string_view(reinterpret_cast<const char *>(data_ + pos_), n);
        pos_ += n;
        return true;
    }

    /// (U16BE length) + bytes
    bool readStringU16(std::string_view &out) noexcept
    {
        std::uint16_t len = 0;
        if (!readU16Be(len))
            return false;
        return readBytes(out, static_cast<std::size_t>(len));
    }

  private:
    static const std::uint8_t *empty_() noexcept
    {
        static const std::uint8_t kEmpty[1] = {0};
        return kEmpty;
    }

  private:
    const std::uint8_t *data_{empty_()};
    std::size_t size_{0};
    std::size_t pos_{0};
};
} // namespace hyperapp::protocol