#include "image.hpp"

#include "../codec/binary.hpp"

#include <array>
#include <stdexcept>

namespace engine::world::storage {
namespace {

struct Gif {
    Image image;
    std::vector<std::uint32_t> palette;
    unsigned background = 0;
    int transparent = -1;
};

std::vector<std::uint32_t> color_table(Reader& reader, unsigned count) {
    std::vector<std::uint32_t> result;
    for (unsigned index = 0; index < count; ++index) {
        const auto red = reader.u8();
        const auto green = reader.u8();
        const auto blue = reader.u8();
        result.push_back((std::uint32_t{red} << 16) | (std::uint32_t{green} << 8) | blue);
    }
    return result;
}

std::vector<std::byte> sub_blocks(Reader& reader) {
    std::vector<std::byte> result;
    for (auto count = reader.u8(); count != 0; count = reader.u8()) {
        const auto part = reader.take(count);
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

struct Lzw {
    std::span<const std::byte> bytes;
    std::size_t bit = 0;
    std::array<std::uint16_t, 4096> prefix{};
    std::array<std::uint8_t, 4096> suffix{};
    std::array<std::uint8_t, 4096> decoded{};
    unsigned clear = 0;
    unsigned next = 0;
    unsigned width = 0;
    int previous = -1;
};

unsigned read_code(Lzw& lzw) {
    if (lzw.bit + lzw.width > lzw.bytes.size() * 8) {
        throw std::runtime_error("Truncated GIF LZW stream");
    }
    unsigned code = 0;
    for (unsigned bit = 0; bit < lzw.width; ++bit) {
        code |= ((std::to_integer<unsigned>(lzw.bytes[lzw.bit / 8]) >> (lzw.bit % 8)) & 1U) << bit;
        ++lzw.bit;
    }
    return code;
}

std::span<const std::uint8_t> expand(Lzw& lzw, unsigned code) {
    std::size_t count = 0;
    while (code >= lzw.clear) {
        if (code >= lzw.next || count >= 4095) {
            throw std::runtime_error("Invalid GIF LZW dictionary chain");
        }
        lzw.decoded[count++] = lzw.suffix[code];
        code = lzw.prefix[code];
    }
    lzw.decoded[count++] = static_cast<std::uint8_t>(code);
    return std::span{lzw.decoded}.first(count);
}

void append_code(Lzw& lzw, unsigned code, std::vector<std::uint8_t>& output, std::size_t limit) {
    if (code > lzw.next || (code == lzw.next && lzw.previous < 0)) {
        throw std::runtime_error("Invalid GIF LZW code");
    }
    const auto source = code == lzw.next ? static_cast<unsigned>(lzw.previous) : code;
    auto reversed = expand(lzw, source);
    const auto first = reversed.back();
    if (output.size() + reversed.size() + (code == lzw.next) > limit) {
        throw std::runtime_error("GIF raster exceeds declared dimensions");
    }
    output.insert(output.end(), reversed.rbegin(), reversed.rend());
    if (code == lzw.next) {
        output.push_back(first);
    }
    if (lzw.previous >= 0 && lzw.next < 4096) {
        lzw.prefix[lzw.next] = static_cast<std::uint16_t>(lzw.previous);
        lzw.suffix[lzw.next++] = first;
        if (lzw.next == (1U << lzw.width) && lzw.width < 12) {
            ++lzw.width;
        }
    }
    lzw.previous = static_cast<int>(code);
}

std::vector<std::uint8_t> decompress(Reader& reader, std::size_t count) {
    const auto minimum = reader.u8();
    if (minimum < 2 || minimum > 8) {
        throw std::runtime_error("GIF LZW minimum code size must be 2..8");
    }
    const auto bytes = sub_blocks(reader);
    Lzw lzw;
    lzw.bytes = bytes;
    lzw.clear = 1U << minimum;
    lzw.width = minimum + 1U;
    lzw.next = lzw.clear + 2;
    std::vector<std::uint8_t> result;
    result.reserve(count);
    while (true) {
        const auto code = read_code(lzw);
        if (code == lzw.clear) {
            lzw.next = lzw.clear + 2;
            lzw.width = minimum + 1U;
            lzw.previous = -1;
        } else if (code == lzw.clear + 1) {
            break;
        } else {
            append_code(lzw, code, result, count);
        }
    }
    if (result.size() != count) {
        throw std::runtime_error("GIF raster has missing pixels");
    }
    return result;
}

void extension(Gif& gif, Reader& reader) {
    const auto type = reader.u8();
    const auto data = sub_blocks(reader);
    if (type == 0xF9) {
        if (data.size() != 4) {
            throw std::runtime_error("Invalid GIF graphic control extension");
        }
        gif.transparent = (std::to_integer<unsigned>(data[0]) & 1U) != 0
                              ? static_cast<int>(std::to_integer<unsigned>(data[3]))
                              : -1;
    }
}

struct Frame {
    unsigned left = 0;
    unsigned top = 0;
    unsigned width = 0;
    unsigned height = 0;
    unsigned flags = 0;
};

void copy_row(Gif& gif, const Frame& frame, unsigned row, std::span<const std::uint8_t> indices) {
    for (unsigned x = 0; x < frame.width; ++x) {
        const auto index = indices[x];
        if (index >= gif.palette.size()) {
            throw std::runtime_error("GIF palette index outside color table");
        }
        const auto color = index == gif.transparent ? 0 : gif.palette[index];
        gif.image
            .pixels[static_cast<std::size_t>(row + frame.top) * gif.image.width + frame.left + x] =
            color;
    }
}

void frame(Gif& gif, Reader& reader) {
    const Frame frame{reader.u16(), reader.u16(), reader.u16(), reader.u16(), reader.u8()};
    if (frame.width == 0 || frame.height == 0 || frame.left + frame.width > gif.image.width ||
        frame.top + frame.height > gif.image.height) {
        throw std::runtime_error("GIF frame lies outside logical screen");
    }
    if ((frame.flags & 128U) != 0) {
        gif.palette = color_table(reader, 2U << (frame.flags & 7U));
    }
    const auto indices = decompress(reader, static_cast<std::size_t>(frame.width) * frame.height);
    constexpr std::array<unsigned, 4> starts{0, 4, 2, 1};
    constexpr std::array<unsigned, 4> strides{8, 8, 4, 2};
    const auto interlaced = (frame.flags & 64U) != 0;
    std::size_t source = 0;
    for (unsigned pass = 0; pass < (interlaced ? 4U : 1U); ++pass) {
        const auto stride = interlaced ? strides[pass] : 1U;
        for (unsigned row = interlaced ? starts[pass] : 0U; row < frame.height; row += stride) {
            copy_row(gif, frame, row, std::span{indices}.subspan(source, frame.width));
            source += frame.width;
        }
    }
}

Gif header(Reader& reader) {
    const auto magic = reader.string(6);
    if (magic != "GIF87a" && magic != "GIF89a") {
        throw std::runtime_error("Invalid GIF signature");
    }
    Gif gif;
    gif.image.width = reader.u16();
    gif.image.height = reader.u16();
    const auto flags = reader.u8();
    gif.background = reader.u8();
    static_cast<void>(reader.u8());
    if (gif.image.width == 0 || gif.image.height == 0 || gif.image.width > 32768 ||
        gif.image.height > 32768) {
        throw std::runtime_error("Unsupported GIF dimensions");
    }
    if ((flags & 128U) != 0) {
        gif.palette = color_table(reader, 2U << (flags & 7U));
    }
    const auto background =
        gif.background < gif.palette.size() && gif.background > 0 ? gif.palette[gif.background] : 0;
    gif.image.pixels.resize(static_cast<std::size_t>(gif.image.width) * gif.image.height,
                            background);
    return gif;
}

} // namespace

Image decode_gif(std::span<const std::byte> bytes) {
    Reader reader{bytes};
    auto gif = header(reader);
    while (reader.remaining() != 0) {
        const auto marker = reader.u8();
        if (marker == 0x21) {
            extension(gif, reader);
        } else if (marker == 0x2C) {
            frame(gif, reader);
            return std::move(gif.image);
        } else {
            throw std::runtime_error("GIF ended before its first image frame");
        }
    }
    throw std::runtime_error("GIF image frame missing");
}

} // namespace engine::world::storage
