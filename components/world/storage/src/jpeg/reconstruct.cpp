#include "decoder.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::world::storage::jpeg {
namespace {

using Cosines = std::array<std::array<double, 8>, 8>;

Cosines make_cosine_table() {
    Cosines result{};
    for (unsigned sample = 0; sample < 8; ++sample) {
        for (unsigned frequency = 0; frequency < 8; ++frequency) {
            const auto angle = (2 * sample + 1) * frequency * std::numbers::pi / 16.0;
            const auto scale = frequency == 0 ? std::numbers::sqrt2 / 2 : 1;
            result[sample][frequency] = scale * std::cos(angle);
        }
    }
    return result;
}

const Cosines& cosine_table() {
    static const auto table = make_cosine_table();
    return table;
}

std::uint8_t channel(double value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

std::array<double, 64> vertical_transform(const Component& component, const Block& block) {
    std::array<double, 64> result{};
    const auto& cosine = cosine_table();
    for (unsigned y = 0; y < 8; ++y) {
        for (unsigned u = 0; u < 8; ++u) {
            for (unsigned v = 0; v < 8; ++v) {
                const auto index = v * 8 + u;
                result[y * 8 + u] += static_cast<double>(block[index]) *
                                     component.quantization_values[index] * cosine[y][v];
            }
        }
    }
    return result;
}

void transform_block(Component& component, unsigned block_index) {
    const auto intermediate = vertical_transform(component, component.blocks[block_index]);
    const auto& cosine = cosine_table();
    const auto stride = component.columns * 8;
    const auto start_x = (block_index % component.columns) * 8;
    const auto start_y = (block_index / component.columns) * 8;
    for (unsigned y = 0; y < 8; ++y) {
        for (unsigned x = 0; x < 8; ++x) {
            double value = 0;
            for (unsigned u = 0; u < 8; ++u) {
                value += intermediate[y * 8 + u] * cosine[x][u];
            }
            component.samples[(start_y + y) * stride + start_x + x] = channel(value / 4 + 128);
        }
    }
}

void reconstruct_planes(Decoder& decoder) {
    for (auto& component : decoder.components) {
        component.samples.resize(component.blocks.size() * 64);
        for (unsigned index = 0; index < component.blocks.size(); ++index) {
            transform_block(component, index);
        }
        component.blocks.clear();
        component.blocks.shrink_to_fit();
    }
}

double sample_component(const Component& component, double x, double y) {
    x = std::clamp(x, 0.0, static_cast<double>(component.sample_width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(component.sample_height - 1));
    const auto x0 = static_cast<unsigned>(x);
    const auto y0 = static_cast<unsigned>(y);
    const auto x1 = std::min(x0 + 1, component.sample_width - 1);
    const auto y1 = std::min(y0 + 1, component.sample_height - 1);
    const auto stride = component.columns * 8;
    const auto first =
        std::lerp(component.samples[y0 * stride + x0], component.samples[y0 * stride + x1], x - x0);
    const auto second =
        std::lerp(component.samples[y1 * stride + x0], component.samples[y1 * stride + x1], x - x0);
    return std::lerp(first, second, y - y0);
}

std::array<double, 4> channels(const Decoder& decoder, unsigned x, unsigned y) {
    std::array<double, 4> result{};
    for (unsigned index = 0; index < decoder.components.size(); ++index) {
        const auto& component = decoder.components[index];
        const auto sample_x = (x + 0.5) * component.horizontal / decoder.max_horizontal - 0.5;
        const auto sample_y = (y + 0.5) * component.vertical / decoder.max_vertical - 0.5;
        result[index] = sample_component(component, sample_x, sample_y);
    }
    return result;
}

std::array<double, 3> convert_ycbcr(const std::array<double, 4>& values) {
    return {values[0] + 1.402 * (values[2] - 128),
            values[0] - 0.344136 * (values[1] - 128) - 0.714136 * (values[2] - 128),
            values[0] + 1.772 * (values[1] - 128)};
}

std::array<double, 3> convert_cmyk(const Decoder& decoder, const std::array<double, 4>& values) {
    std::array<double, 3> result{values[0], values[1], values[2]};
    if (decoder.adobe_transform == 2) {
        result = convert_ycbcr(values);
        for (auto& value : result) {
            value = (255 - std::clamp(value, 0.0, 255.0)) * values[3] / 255;
        }
    } else {
        for (auto& value : result) {
            value = decoder.adobe_transform == 0 ? value * values[3] / 255
                                                 : (255 - value) * (255 - values[3]) / 255;
        }
    }
    return result;
}

std::uint32_t pixel(const Decoder& decoder, const std::array<double, 4>& values) {
    std::array<double, 3> rgb{values[0], values[1], values[2]};
    if (decoder.components.size() == 1) {
        rgb = {values[0], values[0], values[0]};
    } else if (decoder.components.size() == 4) {
        rgb = convert_cmyk(decoder, values);
    } else if (decoder.adobe_transform != 0 && decoder.components[0].id != 'R') {
        rgb = convert_ycbcr(values);
    }
    return (std::uint32_t{channel(rgb[0])} << 16) | (std::uint32_t{channel(rgb[1])} << 8) |
           channel(rgb[2]);
}

} // namespace

Image reconstruct(Decoder& decoder) {
    reconstruct_planes(decoder);
    Image result{decoder.width, decoder.height, {}};
    result.pixels.resize(static_cast<std::size_t>(decoder.width) * decoder.height);
    for (unsigned y = 0; y < decoder.height; ++y) {
        for (unsigned x = 0; x < decoder.width; ++x) {
            result.pixels[static_cast<std::size_t>(y) * decoder.width + x] =
                pixel(decoder, channels(decoder, x, y));
        }
    }
    return result;
}

} // namespace engine::world::storage::jpeg
