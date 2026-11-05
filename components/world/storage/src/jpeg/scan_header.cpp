#include "decoder.hpp"

#include <algorithm>

namespace engine::world::storage::jpeg {
namespace {

void select_component(Scan& scan) {
    const auto id = byte(scan.decoder);
    const auto selectors = byte(scan.decoder);
    const auto found = std::ranges::find(scan.decoder.components, id, &Component::id);
    if (found == scan.decoder.components.end() || (selectors >> 4) > 3 || (selectors & 15) > 3) {
        fail("invalid scan component selector");
    }
    const auto index = static_cast<unsigned>(found - scan.decoder.components.begin());
    if (std::ranges::find(scan.components, index) != scan.components.end()) {
        fail("duplicate scan component");
    }
    found->dc_table = selectors >> 4;
    found->ac_table = selectors & 15;
    scan.components.push_back(index);
}

void validate_progression(const Scan& scan) {
    if (scan.start > scan.end || scan.end > 63 || scan.bit > 13) {
        fail("invalid spectral selection");
    }
    if (scan.start == 0 && scan.end != 0) {
        fail("progressive DC scan contains AC coefficients");
    }
    if (scan.start > 0 && scan.components.size() != 1) {
        fail("progressive AC scan must have one component");
    }
    if (scan.previous_bit != 0 && scan.previous_bit != scan.bit + 1) {
        fail("invalid successive approximation");
    }
}

void validate_coefficients(Scan& scan, Component& component) {
    if (!scan.decoder.quantization_present[component.quantization]) {
        fail("missing quantization table");
    }
    if (!component.quantization_captured) {
        component.quantization_values = scan.decoder.quantization[component.quantization];
        component.quantization_captured = true;
    }
    for (unsigned coefficient = scan.start; coefficient <= scan.end; ++coefficient) {
        const auto previous = component.approximation[coefficient];
        const bool first = scan.previous_bit == 0;
        if ((first && previous != -1) ||
            (!first && previous != static_cast<int>(scan.previous_bit))) {
            fail("inconsistent coefficient scan order");
        }
        component.approximation[coefficient] = static_cast<int>(scan.bit);
    }
    component.predictor = 0;
}

} // namespace

void read_scan(Decoder& decoder, std::size_t end) {
    if (!decoder.frame_seen || end - decoder.position < 4) {
        fail("scan precedes frame");
    }
    Scan scan(decoder);
    const auto count = byte(decoder);
    if (count == 0 || count > decoder.components.size() ||
        end - decoder.position != count * 2 + 3) {
        fail("invalid scan header size");
    }
    for (unsigned index = 0; index < count; ++index) {
        select_component(scan);
    }
    scan.start = byte(decoder);
    scan.end = byte(decoder);
    const auto approximation = byte(decoder);
    scan.previous_bit = approximation >> 4;
    scan.bit = approximation & 15;
    if (decoder.progressive) {
        validate_progression(scan);
    } else if (scan.start != 0 || scan.end != 63 || approximation != 0) {
        fail("invalid sequential scan parameters");
    }
    for (const auto index : scan.components) {
        validate_coefficients(scan, decoder.components[index]);
    }
    decode_scan(scan);
    decoder.scan_seen = true;
}

} // namespace engine::world::storage::jpeg
