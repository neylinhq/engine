#include "decoder.hpp"

#include <stdexcept>
#include <string>

namespace engine::world::storage::jpeg {

[[noreturn]] void fail(const char* reason) {
    throw std::runtime_error(std::string("JPEG: ") + reason);
}

unsigned byte(Decoder& decoder) {
    if (decoder.position >= decoder.bytes.size()) {
        fail("truncated stream");
    }
    return decoder.bytes[decoder.position++];
}

unsigned word(Decoder& decoder) {
    const auto high = byte(decoder);
    return high * 256 + byte(decoder);
}

unsigned marker(Decoder& decoder) {
    if (byte(decoder) != 255) {
        fail("expected marker");
    }
    auto value = byte(decoder);
    while (value == 255) {
        value = byte(decoder);
    }
    if (value == 0) {
        fail("stuffed byte outside entropy stream");
    }
    return value;
}

unsigned bits(Scan& scan, unsigned count) {
    if (count > 16) {
        fail("invalid entropy bit count");
    }
    unsigned result = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (scan.remaining_bits == 0) {
            scan.entropy_byte = byte(scan.decoder);
            if (scan.entropy_byte == 255 && byte(scan.decoder) != 0) {
                fail("unexpected marker in entropy data");
            }
            scan.remaining_bits = 8;
        }
        --scan.remaining_bits;
        result = (result << 1) | ((scan.entropy_byte >> scan.remaining_bits) & 1);
    }
    return result;
}

int amplitude(Scan& scan, unsigned count) {
    if (count == 0) {
        return 0;
    }
    const auto value = static_cast<int>(bits(scan, count));
    const auto threshold = 1 << (count - 1);
    return value < threshold ? value - ((1 << count) - 1) : value;
}

unsigned symbol(Scan& scan, const Huffman& table) {
    if (!table.present) {
        fail("missing Huffman table");
    }
    int code = 0;
    for (unsigned length = 1; length <= 16; ++length) {
        code = code * 2 + static_cast<int>(bits(scan, 1));
        if (table.maximum[length] >= 0 && code <= table.maximum[length]) {
            const auto index = table.first[length] + code - table.minimum[length];
            if (index < 0 || static_cast<std::size_t>(index) >= table.symbols.size()) {
                fail("invalid Huffman code");
            }
            return table.symbols[static_cast<std::size_t>(index)];
        }
    }
    fail("unterminated Huffman code");
}

} // namespace engine::world::storage::jpeg
