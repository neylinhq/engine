#include "binary.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <engine/platform/filesystem/filesystem.hpp>
#include <stdexcept>

namespace engine::world::storage {
namespace {

std::size_t non_ascii_sequence(std::string_view value, std::size_t start) {
    const auto first = static_cast<unsigned char>(value[start]);
    const auto count = first >= 0xF0 && first <= 0xF4  ? 4U
                       : first >= 0xE0 && first < 0xF0 ? 3U
                       : first >= 0xC2 && first < 0xE0 ? 2U
                                                       : 1U;
    if (start + count > value.size()) {
        return 1;
    }
    for (unsigned index = 1; index < count; ++index) {
        if ((static_cast<unsigned char>(value[start + index]) & 0xC0U) != 0x80U) {
            return 1;
        }
    }
    return count;
}

} // namespace

Reader::Reader(std::span<const std::byte> data) : data_(data) {}

std::size_t Reader::remaining() const {
    return data_.size() - position_;
}

std::size_t Reader::position() const {
    return position_;
}

std::span<const std::byte> Reader::take(std::size_t count) {
    if (count > remaining()) {
        throw std::runtime_error("Truncated binary file");
    }
    const auto result = data_.subspan(position_, count);
    position_ += count;
    return result;
}

std::uint8_t Reader::u8() {
    return std::to_integer<std::uint8_t>(take(1)[0]);
}

std::uint8_t Reader::peek() const {
    if (remaining() == 0) {
        throw std::runtime_error("Truncated binary file");
    }
    return std::to_integer<std::uint8_t>(data_[position_]);
}

std::uint16_t Reader::u16() {
    const auto low = u8();
    return static_cast<std::uint16_t>(low | (static_cast<unsigned>(u8()) << 8));
}

std::uint32_t Reader::u32() {
    const auto low = u16();
    return low | (static_cast<std::uint32_t>(u16()) << 16);
}

std::uint64_t Reader::u64() {
    const auto low = u32();
    return low | (static_cast<std::uint64_t>(u32()) << 32);
}

std::int32_t Reader::i32() {
    return std::bit_cast<std::int32_t>(u32());
}

float Reader::f32() {
    return std::bit_cast<float>(u32());
}

std::int32_t Reader::big_i32() {
    return std::bit_cast<std::int32_t>(std::byteswap(u32()));
}

std::string Reader::string(std::size_t count) {
    const auto bytes = take(count);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string Reader::terminated() {
    std::string result;
    while (true) {
        const auto character = u8();
        if (character == 0) {
            return result;
        }
        if (result.size() > kMaxTerminatedStringBytes) {
            throw std::runtime_error("Unbounded binary string");
        }
        if (character >= 128) {
            result.push_back(static_cast<char>(0xC0U | (character >> 6)));
            result.push_back(static_cast<char>(0x80U | (character & 63U)));
        } else {
            result.push_back(static_cast<char>(character));
        }
    }
}

void Writer::u8(std::uint8_t value) {
    bytes_.push_back(static_cast<std::byte>(value));
}

void Writer::u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8));
}

void Writer::u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value));
    u16(static_cast<std::uint16_t>(value >> 16));
}

void Writer::u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32));
}

void Writer::i32(std::int32_t value) {
    u32(std::bit_cast<std::uint32_t>(value));
}

void Writer::f32(float value) {
    u32(std::bit_cast<std::uint32_t>(value));
}

void Writer::string(const std::string& value) {
    append(std::as_bytes(std::span{value}));
}

void Writer::terminated(const std::string& value) {
    for (std::size_t index = 0; index < value.size();) {
        const auto character = static_cast<unsigned char>(value[index]);
        if (character == 0) {
            throw std::runtime_error("Null character inside binary name");
        }
        if (character < 128) {
            u8(character);
            ++index;
        } else {
            const auto count = non_ascii_sequence(value, index);
            u8('?');
            if (count == 4) {
                u8('?');
            }
            index += count;
        }
    }
    u8(0);
}

void Writer::append(std::span<const std::byte> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

const std::vector<std::byte>& Writer::bytes() const {
    return bytes_;
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    constexpr std::size_t kMaxAssetBytes = 8ULL * 1024 * 1024 * 1024;
    return platform::filesystem::read(path, kMaxAssetBytes);
}

std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path{std::u8string{text.begin(), text.end()}};
}

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string extension_text(const std::filesystem::path& path) {
    auto extension = path_text(path.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

} // namespace engine::world::storage
