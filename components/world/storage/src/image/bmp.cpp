#include "image.hpp"

#include "../codec/binary.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <stdexcept>

namespace engine::world::storage {
namespace {

struct Bitmap {
    Image image;
    unsigned depth = 0;
    unsigned compression = 0;
    unsigned colors_used = 0;
    bool top_down = false;
    std::array<std::uint32_t, 3> masks{};
    std::vector<std::uint32_t> palette;
    std::span<const std::byte> pixels;
    std::size_t stride = 0;
};

void dimensions(Bitmap& bitmap, std::int64_t width, std::int64_t height) {
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768) {
        throw std::runtime_error("BMP texture dimensions outside 1..32768");
    }
    bitmap.image.width = static_cast<unsigned>(width);
    bitmap.image.height = static_cast<unsigned>(height);
}

void parse_header(Bitmap& bitmap, Reader& dib, unsigned header_size) {
    std::int64_t width = header_size == 12 ? dib.u16() : dib.i32();
    std::int64_t height = header_size == 12 ? dib.u16() : dib.i32();
    bitmap.top_down = height < 0;
    dimensions(bitmap, width, std::abs(height));
    const auto planes = dib.u16();
    bitmap.depth = dib.u16();
    if (header_size != 12) {
        bitmap.compression = dib.u32();
        static_cast<void>(dib.take(12));
        bitmap.colors_used = dib.u32();
        static_cast<void>(dib.u32());
    }
    if (planes != 1 || (bitmap.compression != 0 && bitmap.compression != 3) ||
        (bitmap.depth != 1 && bitmap.depth != 4 && bitmap.depth != 8 && bitmap.depth != 16 &&
         bitmap.depth != 24 && bitmap.depth != 32)) {
        throw std::runtime_error("Unsupported BMP encoding");
    }
    if (bitmap.compression == 3 && bitmap.depth != 16 && bitmap.depth != 32) {
        throw std::runtime_error("BMP bitfields require RGB16 or RGB32");
    }
}

void read_masks(Bitmap& bitmap, Reader& file, Reader& dib, unsigned header_size) {
    if (bitmap.depth != 16 && bitmap.depth != 32) {
        return;
    }
    if (bitmap.compression == 3) {
        auto& reader = header_size >= 52 ? dib : file;
        for (auto& mask : bitmap.masks) {
            mask = reader.u32();
        }
    } else {
        bitmap.masks = bitmap.depth == 16 ? std::array<std::uint32_t, 3>{0x7C00U, 0x3E0U, 0x1FU}
                                          : std::array<std::uint32_t, 3>{0xFF0000U, 0xFF00U, 0xFFU};
    }
    std::uint32_t occupied = 0;
    for (auto mask : bitmap.masks) {
        if (mask == 0 || (occupied & mask) != 0 || (bitmap.depth == 16 && mask > 0xFFFFU)) {
            throw std::runtime_error("Invalid or overlapping BMP color masks");
        }
        const auto bits = mask >> std::countr_zero(mask);
        if ((bits & (bits + 1)) != 0 || std::popcount(bits) > 8) {
            throw std::runtime_error("BMP color masks must be contiguous with at most 8 bits");
        }
        occupied |= mask;
    }
}

void read_palette(Bitmap& bitmap, Reader& reader, unsigned offset, unsigned entry_bytes) {
    if (bitmap.depth > 8) {
        return;
    }
    if (offset < reader.position()) {
        throw std::runtime_error("BMP palette overlaps header");
    }
    auto count = bitmap.colors_used;
    if (count == 0) {
        count = std::min(1U << bitmap.depth,
                         static_cast<unsigned>((offset - reader.position()) / entry_bytes));
    }
    if (count == 0 || count > (1U << bitmap.depth) ||
        count > (offset - reader.position()) / entry_bytes) {
        throw std::runtime_error("Invalid BMP palette size");
    }
    for (unsigned index = 0; index < count; ++index) {
        const auto blue = reader.u8();
        const auto green = reader.u8();
        const auto red = reader.u8();
        if (entry_bytes == 4) {
            static_cast<void>(reader.u8());
        }
        bitmap.palette.push_back((std::uint32_t{red} << 16) | (std::uint32_t{green} << 8) | blue);
    }
}

Bitmap parse(std::span<const std::byte> bytes) {
    Reader reader{bytes};
    if (reader.u16() != 0x4D42) {
        throw std::runtime_error("Invalid BMP signature");
    }
    static_cast<void>(reader.take(8));
    const auto offset = reader.u32();
    const auto header_size = reader.u32();
    if (header_size != 12 && header_size < 40) {
        throw std::runtime_error("Unsupported BMP DIB header");
    }
    Reader dib{reader.take(header_size - 4)};
    Bitmap bitmap;
    parse_header(bitmap, dib, header_size);
    read_masks(bitmap, reader, dib, header_size);
    read_palette(bitmap, reader, offset, header_size == 12 ? 3U : 4U);
    bitmap.stride = (static_cast<std::size_t>(bitmap.image.width) * bitmap.depth + 31) / 32 * 4;
    if (offset < reader.position() || offset > bytes.size() ||
        bitmap.stride * bitmap.image.height > bytes.size() - offset) {
        throw std::runtime_error("Truncated BMP raster or invalid pixel offset");
    }
    bitmap.pixels = bytes.subspan(offset);
    bitmap.image.pixels.resize(static_cast<std::size_t>(bitmap.image.width) * bitmap.image.height);
    return bitmap;
}

unsigned channel(std::uint32_t value, std::uint32_t mask) {
    const auto width = std::popcount(mask);
    value = (value & mask) >> std::countr_zero(mask);
    auto result = value;
    auto available = width;
    while (available < 8) {
        result = (result << width) | value;
        available += width;
    }
    return result >> (available - 8);
}

std::uint32_t pixel(const Bitmap& bitmap, std::span<const std::byte> row, unsigned x) {
    if (bitmap.depth <= 8) {
        const auto bit = x * bitmap.depth;
        const auto value =
            (std::to_integer<unsigned>(row[bit / 8]) >> (8 - bitmap.depth - bit % 8)) &
            ((1U << bitmap.depth) - 1);
        if (value >= bitmap.palette.size()) {
            throw std::runtime_error("BMP pixel index exceeds palette");
        }
        return bitmap.palette[value];
    }
    Reader reader{row.subspan(static_cast<std::size_t>(x) * bitmap.depth / 8)};
    if (bitmap.depth == 24) {
        const auto blue = reader.u8();
        const auto green = reader.u8();
        const auto red = reader.u8();
        return (std::uint32_t{red} << 16) | (std::uint32_t{green} << 8) | blue;
    }
    const auto value = bitmap.depth == 16 ? reader.u16() : reader.u32();
    return (channel(value, bitmap.masks[0]) << 16) | (channel(value, bitmap.masks[1]) << 8) |
           channel(value, bitmap.masks[2]);
}

} // namespace

Image decode_bmp(std::span<const std::byte> bytes) {
    auto bitmap = parse(bytes);
    for (unsigned row = 0; row < bitmap.image.height; ++row) {
        const auto pixels = bitmap.pixels.subspan(row * bitmap.stride, bitmap.stride);
        const auto y = bitmap.top_down ? row : bitmap.image.height - row - 1;
        for (unsigned x = 0; x < bitmap.image.width; ++x) {
            bitmap.image.pixels[static_cast<std::size_t>(y) * bitmap.image.width + x] =
                pixel(bitmap, pixels, x);
        }
    }
    return std::move(bitmap.image);
}

} // namespace engine::world::storage
