#include "image.hpp"

#include "../codec/zip.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <stdexcept>

namespace engine::world::storage {
namespace {

struct Png {
    Image image;
    unsigned depth = 0;
    unsigned type = 0;
    unsigned interlace = 0;
    unsigned channels = 0;
    std::vector<std::uint32_t> palette;
    std::vector<std::byte> compressed;
};

void header(Png& png, Reader& reader) {
    png.image.width = std::byteswap(reader.u32());
    png.image.height = std::byteswap(reader.u32());
    png.depth = reader.u8();
    png.type = reader.u8();
    const auto compression = reader.u8();
    const auto filter = reader.u8();
    png.interlace = reader.u8();
    constexpr std::array<unsigned, 7> kChannels{1, 0, 3, 1, 2, 0, 4};
    if (png.type >= kChannels.size() || kChannels[png.type] == 0) {
        throw std::runtime_error("Invalid PNG color type");
    }
    png.channels = kChannels[png.type];
    if (png.image.width == 0 || png.image.height == 0 || png.image.width > 32768 ||
        png.image.height > 32768 || compression != 0 || filter != 0 || png.interlace > 1) {
        throw std::runtime_error("Unsupported PNG dimensions or header");
    }
    if (png.depth != 1 && png.depth != 2 && png.depth != 4 && png.depth != 8 && png.depth != 16) {
        throw std::runtime_error("Invalid PNG bit depth");
    }
    if ((png.type != 0 && png.type != 3 && png.depth < 8) || (png.type == 3 && png.depth == 16)) {
        throw std::runtime_error("Invalid PNG bit depth for color type");
    }
}

void parse_chunk(Png& png, const std::string& type, std::span<const std::byte> bytes) {
    Reader reader{bytes};
    if (type == "IHDR") {
        header(png, reader);
    } else if (type == "IDAT") {
        png.compressed.insert(png.compressed.end(), bytes.begin(), bytes.end());
    } else if (type == "PLTE") {
        if (bytes.size() % 3 != 0 || bytes.size() > 768) {
            throw std::runtime_error("Invalid PNG palette");
        }
        while (reader.remaining() != 0) {
            const auto red = reader.u8();
            const auto green = reader.u8();
            const auto blue = reader.u8();
            png.palette.push_back((std::uint32_t{red} << 16) | (std::uint32_t{green} << 8) | blue);
        }
    }
}

unsigned paeth(unsigned left, unsigned up, unsigned upper_left) {
    const auto p = static_cast<int>(left + up) - static_cast<int>(upper_left);
    const auto a = std::abs(p - static_cast<int>(left));
    const auto b = std::abs(p - static_cast<int>(up));
    const auto c = std::abs(p - static_cast<int>(upper_left));
    if (a <= b && a <= c) {
        return left;
    }
    return b <= c ? up : upper_left;
}

void unfilter(std::span<std::byte> row, std::span<const std::byte> previous, unsigned stride,
              unsigned filter) {
    if (filter > 4) {
        throw std::runtime_error("Invalid PNG row filter");
    }
    for (std::size_t index = 0; index < row.size(); ++index) {
        const auto left = index >= stride ? std::to_integer<unsigned>(row[index - stride]) : 0;
        const auto up = previous.empty() ? 0 : std::to_integer<unsigned>(previous[index]);
        const auto corner = previous.empty() || index < stride
                                ? 0
                                : std::to_integer<unsigned>(previous[index - stride]);
        unsigned prediction = 0;
        if (filter == 1) {
            prediction = left;
        } else if (filter == 2) {
            prediction = up;
        } else if (filter == 3) {
            prediction = (left + up) / 2;
        } else if (filter == 4) {
            prediction = paeth(left, up, corner);
        }
        row[index] =
            static_cast<std::byte>((std::to_integer<unsigned>(row[index]) + prediction) & 255);
    }
}

unsigned sample(std::span<const std::byte> row, unsigned index, unsigned depth) {
    if (depth == 16) {
        return std::to_integer<unsigned>(row[index * 2]);
    }
    if (depth == 8) {
        return std::to_integer<unsigned>(row[index]);
    }
    const auto bit = index * depth;
    return (std::to_integer<unsigned>(row[bit / 8]) >> (8 - depth - bit % 8)) & ((1U << depth) - 1);
}

std::uint32_t pixel(const Png& png, std::span<const std::byte> row, unsigned x) {
    const auto first = sample(row, x * png.channels, png.depth);
    if (png.type == 3) {
        if (first >= png.palette.size()) {
            throw std::runtime_error("PNG palette index outside palette");
        }
        return png.palette[first];
    }
    if (png.type == 0 || png.type == 4) {
        const auto gray = png.depth < 8 ? first * 255 / ((1U << png.depth) - 1) : first;
        return gray * 0x010101U;
    }
    const auto green = sample(row, x * png.channels + 1, png.depth);
    const auto blue = sample(row, x * png.channels + 2, png.depth);
    return (first << 16) | (green << 8) | blue;
}

struct Pass {
    unsigned x = 0;
    unsigned y = 0;
    unsigned dx = 1;
    unsigned dy = 1;
};

std::array<Pass, 7> passes(unsigned interlace) {
    if (interlace == 0) {
        return {{{0, 0, 1, 1}, {}, {}, {}, {}, {}, {}}};
    }
    return {{{0, 0, 8, 8},
             {4, 0, 8, 8},
             {0, 4, 4, 8},
             {2, 0, 4, 4},
             {0, 2, 2, 4},
             {1, 0, 2, 2},
             {0, 1, 1, 2}}};
}

unsigned extent(unsigned size, unsigned start, unsigned stride) {
    return size > start ? (size - start + stride - 1) / stride : 0;
}

std::size_t raw_size(const Png& png) {
    std::size_t result = 0;
    const auto values = passes(png.interlace);
    const auto count = png.interlace == 0 ? 1U : 7U;
    for (unsigned index = 0; index < count; ++index) {
        const auto pass = values[index];
        const auto width = extent(png.image.width, pass.x, pass.dx);
        const auto height = extent(png.image.height, pass.y, pass.dy);
        if (width != 0 && height != 0) {
            result +=
                ((static_cast<std::size_t>(width) * png.channels * png.depth + 7) / 8 + 1) * height;
        }
    }
    return result;
}

void apply_pass(Png& png, Reader& data, Pass pass) {
    const auto width = extent(png.image.width, pass.x, pass.dx);
    const auto height = extent(png.image.height, pass.y, pass.dy);
    if (width == 0 || height == 0) {
        return;
    }
    const auto row_bytes = (static_cast<std::size_t>(width) * png.channels * png.depth + 7) / 8;
    std::vector<std::byte> previous;
    for (unsigned y = 0; y < height; ++y) {
        const auto filter = data.u8();
        const auto packed = data.take(row_bytes);
        std::vector<std::byte> row(packed.begin(), packed.end());
        unfilter(row, previous, std::max(1U, png.channels * png.depth / 8), filter);
        for (unsigned x = 0; x < width; ++x) {
            png.image.pixels[static_cast<std::size_t>(pass.y + y * pass.dy) * png.image.width +
                             pass.x + x * pass.dx] = pixel(png, row, x);
        }
        previous = std::move(row);
    }
}

std::uint32_t adler32(std::span<const std::byte> bytes) {
    std::uint32_t low = 1;
    std::uint32_t high = 0;
    for (auto byte : bytes) {
        low = (low + std::to_integer<unsigned>(byte)) % 65521;
        high = (high + low) % 65521;
    }
    return (high << 16) | low;
}

std::vector<std::byte> decompress(const Png& png) {
    const auto zlib = std::to_integer<unsigned>(png.compressed[0]) * 256 +
                      std::to_integer<unsigned>(png.compressed[1]);
    if (zlib % 31 != 0 || (zlib & 0x20U) != 0 || ((zlib >> 8) & 15U) != 8 || (zlib >> 12) > 7) {
        throw std::runtime_error("Invalid PNG zlib header");
    }
    const auto compressed = std::span{png.compressed};
    auto raw = inflate(compressed.subspan(2, compressed.size() - 6), raw_size(png));
    Reader checksum{compressed.last(4)};
    if (adler32(raw) != std::byteswap(checksum.u32())) {
        throw std::runtime_error("PNG zlib checksum mismatch");
    }
    return raw;
}

} // namespace

Image decode_png(std::span<const std::byte> bytes) {
    Reader reader{bytes};
    if (reader.u32() != 0x474E5089U || reader.u32() != 0x0A1A0A0DU) {
        throw std::runtime_error("Invalid PNG signature");
    }
    Png png;
    while (reader.remaining() != 0) {
        const auto length = std::byteswap(reader.u32());
        const auto chunk = reader.take(static_cast<std::size_t>(length) + 4);
        const auto checksum = std::byteswap(reader.u32());
        if (checksum_crc32(chunk) != checksum) {
            throw std::runtime_error("PNG checksum mismatch");
        }
        Reader part{chunk};
        const auto type = part.string(4);
        const auto content = part.take(length);
        parse_chunk(png, type, content);
    }
    if (png.image.width == 0 || png.compressed.size() < 6) {
        throw std::runtime_error("PNG image data missing");
    }
    const auto raw = decompress(png);
    Reader data{raw};
    png.image.pixels.resize(static_cast<std::size_t>(png.image.width) * png.image.height);
    const auto values = passes(png.interlace);
    const auto count = png.interlace == 0 ? 1U : 7U;
    for (unsigned index = 0; index < count; ++index) {
        apply_pass(png, data, values[index]);
    }
    return std::move(png.image);
}

} // namespace engine::world::storage
