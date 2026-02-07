#pragma once
#include <hypernet/protocol/Endian.hpp>
#include <hypernet/protocol/MessageView.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace hyperapp::protocol
{
class PacketWriter
{
  public:
    void clear() { buf_.clear(); }
    void reserve(std::size_t n) { buf_.reserve(n); }
    [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }

    void writeU8(std::uint8_t v) { buf_.push_back(v); }

    void writeU16Be(std::uint16_t v)
    {
        std::uint8_t tmp[2];
        hypernet::protocol::storeU16Be(v, tmp);
        buf_.insert(buf_.end(), tmp, tmp + 2);
    }

    void writeU32Be(std::uint32_t v)
    {
        std::uint8_t tmp[4];
        hypernet::protocol::storeU32Be(v, tmp);
        buf_.insert(buf_.end(), tmp, tmp + 4);
    }

    void writeU64Be(std::uint64_t v)
    {
        std::uint8_t tmp[8];
        hypernet::protocol::storeU64Be(v, tmp);
        buf_.insert(buf_.end(), tmp, tmp + 8);
    }
    void writeBytes(const void *p, std::size_t n)
    {
        if (n == 0)
            return;
        const auto *b = static_cast<const std::uint8_t *>(p);
        buf_.insert(buf_.end(), b, b + n);
    }

    // 기존 정책 유지: 0xFFFF 초과 시 truncate
    void writeStringU16(std::string_view s)
    {
        if (s.size() > 0xFFFF)
            s = s.substr(0, 0xFFFF);
        writeU16Be(static_cast<std::uint16_t>(s.size()));
        writeBytes(s.data(), s.size());
    }
    [[nodiscard]] bool writeStringU16Checked(std::string_view s, std::uint16_t maxLen) noexcept
    {
        if (s.size() > maxLen)
            return false;
        if (s.size() > 0xFFFF)
            return false;
        writeU16Be(static_cast<std::uint16_t>(s.size()));
        writeBytes(s.data(), s.size());
        return true;
    }

    [[nodiscard]] hypernet::protocol::MessageView view() const noexcept
    {
        if (buf_.empty())
            return {nullptr, 0};
        return {buf_.data(), buf_.size()};
    }

    [[nodiscard]] std::shared_ptr<std::vector<std::uint8_t>> share() const { return std::make_shared<std::vector<std::uint8_t>>(buf_); }

  private:
    std::vector<std::uint8_t> buf_;
};
} // namespace hyperapp::protocol
