#include "decoder.hpp"

#include <algorithm>

namespace engine::world::storage::jpeg {
namespace {

unsigned ceiling(unsigned value, unsigned divisor) {
    return (value + divisor - 1) / divisor;
}

void read_component(Decoder& decoder) {
    Component component;
    component.id = byte(decoder);
    const auto sampling = byte(decoder);
    component.horizontal = sampling >> 4;
    component.vertical = sampling & 15;
    component.quantization = byte(decoder);
    const bool duplicate = std::ranges::any_of(
        decoder.components, [&](const auto& other) { return component.id == other.id; });
    if (duplicate || component.horizontal < 1 || component.horizontal > 4 ||
        component.vertical < 1 || component.vertical > 4 || component.quantization > 3) {
        fail("invalid frame component");
    }
    component.approximation.fill(-1);
    decoder.max_horizontal = std::max(decoder.max_horizontal, component.horizontal);
    decoder.max_vertical = std::max(decoder.max_vertical, component.vertical);
    decoder.components.push_back(std::move(component));
}

void allocate_components(Decoder& decoder) {
    decoder.mcu_columns = ceiling(decoder.width, decoder.max_horizontal * 8);
    decoder.mcu_rows = ceiling(decoder.height, decoder.max_vertical * 8);
    std::size_t total_blocks = 0;
    for (auto& component : decoder.components) {
        component.columns = decoder.mcu_columns * component.horizontal;
        component.rows = decoder.mcu_rows * component.vertical;
        component.sample_width =
            ceiling(decoder.width * component.horizontal, decoder.max_horizontal);
        component.sample_height =
            ceiling(decoder.height * component.vertical, decoder.max_vertical);
        component.actual_columns = ceiling(component.sample_width, 8);
        component.actual_rows = ceiling(component.sample_height, 8);
        const auto count = static_cast<std::size_t>(component.columns) * component.rows;
        total_blocks += count;
        if (total_blocks > 4194304) {
            fail("coefficient storage exceeds supported image limit");
        }
        component.blocks.resize(count);
    }
}

} // namespace

void read_frame(Decoder& decoder, std::size_t end, unsigned type) {
    if (decoder.frame_seen || end - decoder.position < 6) {
        fail("invalid or duplicate frame");
    }
    if (byte(decoder) != 8) {
        fail("only 8-bit DCT JPEG is supported");
    }
    decoder.height = word(decoder);
    decoder.width = word(decoder);
    const auto count = byte(decoder);
    if (decoder.width == 0 || decoder.height == 0 || (count != 1 && count != 3 && count != 4)) {
        fail("unsupported frame dimensions or component count");
    }
    if (std::uint64_t{decoder.width} * decoder.height > 67108864 ||
        end - decoder.position != count * 3) {
        fail("invalid or excessive frame size");
    }
    for (unsigned index = 0; index < count; ++index) {
        read_component(decoder);
    }
    decoder.progressive = type == 0xc2;
    decoder.frame_seen = true;
    allocate_components(decoder);
}

} // namespace engine::world::storage::jpeg
