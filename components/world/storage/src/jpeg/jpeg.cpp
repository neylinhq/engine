#include "decoder.hpp"

#include <algorithm>
#include <string_view>

namespace engine::world::storage::jpeg {
namespace {

std::size_t segment_end(Decoder& decoder) {
    const auto length = word(decoder);
    if (length < 2 || length - 2 > decoder.bytes.size() - decoder.position) {
        fail("invalid segment length");
    }
    return decoder.position + length - 2;
}

void application_marker(Decoder& decoder, std::size_t end, unsigned type) {
    if (type != 0xee || end - decoder.position < 12) {
        return;
    }
    const auto* data = reinterpret_cast<const char*>(decoder.bytes.data() + decoder.position);
    if (std::string_view(data, 5) == "Adobe") {
        decoder.adobe_transform = decoder.bytes[decoder.position + 11];
    }
}

void metadata(Decoder& decoder, std::size_t end, unsigned type) {
    if (type == 0xdd) {
        if (end - decoder.position != 2) {
            fail("invalid restart interval segment");
        }
        decoder.restart_interval = word(decoder);
    } else if ((type >= 0xc0 && type <= 0xcf) || type == 0xdc) {
        fail("unsupported arithmetic, lossless, or hierarchical JPEG coding");
    } else if (type < 0xe0 && type != 0xfe) {
        fail("unsupported JPEG marker");
    }
    application_marker(decoder, end, type);
}

void segment(Decoder& decoder, unsigned type) {
    const auto end = segment_end(decoder);
    if (type == 0xda) {
        read_scan(decoder, end);
        return;
    }
    switch (type) {
    case 0xdb:
        read_quantization(decoder, end);
        break;
    case 0xc4:
        read_huffman(decoder, end);
        break;
    case 0xc0:
    case 0xc1:
    case 0xc2:
        read_frame(decoder, end, type);
        break;
    default:
        metadata(decoder, end, type);
        break;
    }
    decoder.position = end;
}

} // namespace

Image decode(std::span<const std::uint8_t> bytes) {
    Decoder decoder;
    decoder.bytes = bytes;
    if (marker(decoder) != 0xd8) {
        fail("missing start of image");
    }
    auto next = marker(decoder);
    while (next != 0xd9) {
        segment(decoder, next);
        next = marker(decoder);
    }
    if (!decoder.frame_seen || !decoder.scan_seen) {
        fail("image has no frame or scan");
    }
    if (std::ranges::any_of(decoder.components,
                            [](const auto& component) { return component.approximation[0] < 0; })) {
        fail("image component has no DC scan");
    }
    return reconstruct(decoder);
}

} // namespace engine::world::storage::jpeg

namespace engine::world::storage {

Image decode_jpeg(std::span<const std::uint8_t> bytes) {
    return jpeg::decode(bytes);
}

} // namespace engine::world::storage
