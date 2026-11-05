#include "../state.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
void big_endian(Bytes& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(std::byte((value >> shift) & 0xffu));
    }
}

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::uint32_t adler32(std::span<const std::byte> bytes) {
    std::uint32_t first = 1;
    std::uint32_t second = 0;
    for (const auto byte : bytes) {
        first = (first + std::to_integer<std::uint32_t>(byte)) % 65521u;
        second = (second + first) % 65521u;
    }
    return (second << 16u) | first;
}

void chunk(Bytes& output, std::string_view name, std::span<const std::byte> data) {
    big_endian(output, static_cast<std::uint32_t>(data.size()));
    const auto start = output.size();
    for (const auto letter : name) {
        output.push_back(std::byte(letter));
    }
    output.insert(output.end(), data.begin(), data.end());
    big_endian(output, crc32(std::span(output).subspan(start)));
}

Bytes store_deflate(std::span<const std::byte> input) {
    Bytes result{std::byte{0x78}, std::byte{0x01}};
    for (std::size_t offset = 0; offset < input.size();) {
        const auto count = std::min<std::size_t>(65535, input.size() - offset);
        const auto length = static_cast<std::uint16_t>(count);
        const auto inverse = static_cast<std::uint16_t>(~length);
        result.push_back(std::byte(offset + count == input.size() ? 1 : 0));
        result.push_back(std::byte(length & 0xff));
        result.push_back(std::byte(length >> 8));
        result.push_back(std::byte(inverse & 0xff));
        result.push_back(std::byte(inverse >> 8));
        result.insert(result.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                      input.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    big_endian(result, adler32(input));
    return result;
}

Bytes image_rows(vk::Extent2D extent, std::span<const std::byte> pixels, bool blue_first) {
    const auto row_size = static_cast<std::size_t>(extent.width) * 4;
    Bytes result((row_size + 1) * extent.height);
    for (std::size_t row = 0; row < extent.height; ++row) {
        const auto destination = row * (row_size + 1) + 1;
        std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(row * row_size), row_size,
                    result.begin() + static_cast<std::ptrdiff_t>(destination));
        if (blue_first) {
            for (std::size_t pixel = 0; pixel < extent.width; ++pixel) {
                std::swap(result[destination + pixel * 4], result[destination + pixel * 4 + 2]);
            }
        }
    }
    return result;
}
} // namespace

void write_png(const std::filesystem::path& path, vk::Extent2D extent,
               std::span<const std::byte> pixels, bool blue_first) {
    const auto expected = static_cast<std::size_t>(extent.width) * extent.height * 4;
    if (!extent.width || !extent.height || pixels.size() != expected) {
        throw std::runtime_error("Invalid screenshot pixel buffer");
    }
    Bytes output{std::byte{137}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
                 std::byte{13},  std::byte{10},  std::byte{26},  std::byte{10}};
    Bytes header;
    big_endian(header, extent.width);
    big_endian(header, extent.height);
    header.insert(header.end(),
                  {std::byte{8}, std::byte{6}, std::byte{0}, std::byte{0}, std::byte{0}});
    chunk(output, "IHDR", header);
    const auto rows = image_rows(extent, pixels, blue_first);
    const auto encoded = store_deflate(rows);
    chunk(output, "IDAT", encoded);
    chunk(output, "IEND", {});
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(output.data()),
                 static_cast<std::streamsize>(output.size()));
    stream.close();
    if (!stream) {
        throw std::runtime_error("Cannot save PNG screenshot: " + path_text(path));
    }
}

} // namespace engine::rendering::vulkan
