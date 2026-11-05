#include "image.hpp"

#include "../codec/binary.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace engine::world::storage {
namespace {

void dimensions(Image& image, std::int64_t width, std::int64_t height) {
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768) {
        throw std::runtime_error("Texture dimensions outside 1..32768");
    }
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.pixels.resize(static_cast<std::size_t>(width * height));
}

std::string ppm_token(Reader& reader) {
    std::string result;
    while (reader.remaining() != 0) {
        const auto value = reader.u8();
        if (value == '#') {
            while (reader.remaining() != 0 && reader.u8() != '\n') {
            }
            if (!result.empty()) {
                return result;
            }
        } else if (std::isspace(value) != 0) {
            if (!result.empty()) {
                if (value == '\r' && reader.remaining() != 0 && reader.peek() == '\n') {
                    static_cast<void>(reader.u8());
                }
                return result;
            }
        } else {
            result.push_back(static_cast<char>(value));
        }
    }
    return result;
}

unsigned ppm_value(Reader& reader, bool binary, unsigned maximum) {
    unsigned value = 0;
    if (!binary) {
        value = static_cast<unsigned>(std::stoul(ppm_token(reader)));
    } else if (maximum < 256) {
        value = reader.u8();
    } else {
        value = static_cast<unsigned>(reader.u8()) << 8;
        value |= reader.u8();
    }
    if (value > maximum) {
        throw std::runtime_error("PPM color exceeds declared maximum");
    }
    return value * 255 / maximum;
}

} // namespace

Image decode_ppm(std::span<const std::byte> bytes) {
    Reader reader{bytes};
    const auto magic = ppm_token(reader);
    if (magic != "P6" && magic != "P3") {
        throw std::runtime_error("Supported PPM encodings are P3 and P6");
    }
    const auto width = std::stoll(ppm_token(reader));
    const auto height = std::stoll(ppm_token(reader));
    const auto maximum = static_cast<unsigned>(std::stoul(ppm_token(reader)));
    if (maximum == 0 || maximum > 65535) {
        throw std::runtime_error("Invalid PPM color maximum");
    }
    Image image;
    dimensions(image, width, height);
    for (auto& pixel : image.pixels) {
        const auto red = ppm_value(reader, magic == "P6", maximum);
        const auto green = ppm_value(reader, magic == "P6", maximum);
        const auto blue = ppm_value(reader, magic == "P6", maximum);
        pixel = (red << 16) | (green << 8) | blue;
    }
    return image;
}

std::shared_ptr<const Image> load_image(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    const auto extension = extension_text(path);
    if (extension == ".png") {
        return std::make_shared<Image>(decode_png(bytes));
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        const auto data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        return std::make_shared<Image>(decode_jpeg({data, bytes.size()}));
    }
    if (extension == ".bmp") {
        return std::make_shared<Image>(decode_bmp(bytes));
    }
    if (extension == ".gif") {
        return std::make_shared<Image>(decode_gif(bytes));
    }
    if (extension == ".ppm" || extension == ".pnm") {
        return std::make_shared<Image>(decode_ppm(bytes));
    }
    throw std::runtime_error("Unsupported mesh texture format: " + extension);
}

std::uint32_t sample_image(const Image& image, TextureCoordinate coordinate) {
    if (image.width == 0 || image.height == 0 || !std::isfinite(coordinate.u) ||
        !std::isfinite(coordinate.v)) {
        throw std::runtime_error("Invalid texture image or coordinate");
    }
    auto u = static_cast<float>(coordinate.u);
    auto v = 1.0F - static_cast<float>(coordinate.v);
    u -= std::floor(u);
    v -= std::floor(v);
    u += u == 0;
    v += v == 0;
    const auto x = static_cast<unsigned>(u * static_cast<float>(image.width - 1));
    const auto y = static_cast<unsigned>(v * static_cast<float>(image.height - 1));
    return image.pixels[static_cast<std::size_t>(y) * image.width + x];
}

} // namespace engine::world::storage
