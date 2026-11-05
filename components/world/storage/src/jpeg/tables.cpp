#include "decoder.hpp"

namespace engine::world::storage::jpeg {
namespace {

void huffman_lengths(Decoder& decoder, Huffman& table) {
    int code = 0;
    int first = 0;
    for (unsigned length = 1; length <= 16; ++length) {
        const auto count = static_cast<int>(byte(decoder));
        if (code + count > (1 << length)) {
            fail("oversubscribed Huffman tree");
        }
        table.minimum[length] = code;
        table.maximum[length] = count == 0 ? -1 : code + count - 1;
        table.first[length] = first;
        code = (code + count) * 2;
        first += count;
    }
    if (first == 0 || first > 256) {
        fail("invalid Huffman symbol count");
    }
    table.symbols.resize(static_cast<std::size_t>(first));
}

} // namespace

void read_quantization(Decoder& decoder, std::size_t end) {
    while (decoder.position < end) {
        const auto header = byte(decoder);
        const auto index = header & 15;
        const auto precision = header >> 4;
        if (index >= 4 || precision > 1 || end - decoder.position < 64 * (precision + 1)) {
            fail("invalid quantization table");
        }
        for (unsigned coefficient = 0; coefficient < 64; ++coefficient) {
            const auto value = precision == 0 ? byte(decoder) : word(decoder);
            if (value == 0) {
                fail("zero quantization divisor");
            }
            decoder.quantization[index][kZigzag[coefficient]] = static_cast<std::uint16_t>(value);
        }
        decoder.quantization_present[index] = true;
    }
}

void read_huffman(Decoder& decoder, std::size_t end) {
    while (decoder.position < end) {
        if (end - decoder.position < 17) {
            fail("truncated Huffman table");
        }
        const auto header = byte(decoder);
        const auto index = header & 15;
        const auto kind = header >> 4;
        if (index >= 4 || kind > 1) {
            fail("invalid Huffman table selector");
        }
        auto& table = kind == 0 ? decoder.dc_tables[index] : decoder.ac_tables[index];
        huffman_lengths(decoder, table);
        if (table.symbols.size() > end - decoder.position) {
            fail("truncated Huffman symbols");
        }
        for (auto& value : table.symbols) {
            value = static_cast<std::uint8_t>(byte(decoder));
        }
        table.present = true;
    }
}

} // namespace engine::world::storage::jpeg
