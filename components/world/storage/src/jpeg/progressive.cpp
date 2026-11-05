#include "decoder.hpp"

#include <cstdlib>

namespace engine::world::storage::jpeg {
namespace {

void dc_coefficient(Scan& scan, Component& component, Block& block) {
    if (scan.previous_bit != 0) {
        block[0] |= static_cast<std::int32_t>(bits(scan, 1) << scan.bit);
        return;
    }
    const auto category = symbol(scan, scan.decoder.dc_tables[component.dc_table]);
    if (category > 11) {
        fail("invalid progressive DC category");
    }
    component.predictor += amplitude(scan, category);
    const auto coefficient = std::int64_t{component.predictor} * (std::int64_t{1} << scan.bit);
    if (std::abs(coefficient) > 16777216) {
        fail("progressive DC predictor overflow");
    }
    block[0] = static_cast<std::int32_t>(coefficient);
}

bool begin_end_run(Scan& scan, unsigned code, unsigned consumed) {
    const auto run = code >> 4;
    if ((code & 15) != 0 || run == 15) {
        return false;
    }
    scan.end_run = (1u << run) + bits(scan, run) - consumed;
    return true;
}

void first_ac(Scan& scan, Component& component, Block& block) {
    if (scan.end_run != 0) {
        --scan.end_run;
        return;
    }
    auto coefficient = scan.start;
    while (coefficient <= scan.end) {
        const auto code = symbol(scan, scan.decoder.ac_tables[component.ac_table]);
        const auto run = code >> 4;
        const auto size = code & 15;
        if (begin_end_run(scan, code, 1)) {
            return;
        }
        coefficient += size == 0 ? 16 : run;
        if (size > 10 || coefficient > scan.end + 1 || (size != 0 && coefficient > scan.end)) {
            fail("invalid progressive AC run");
        }
        if (size != 0) {
            block[kZigzag[coefficient++]] = amplitude(scan, size) * (1 << scan.bit);
        }
    }
}

void refine_coefficient(Scan& scan, std::int32_t& coefficient) {
    const int increment = 1 << scan.bit;
    if (bits(scan, 1) != 0 && (std::abs(coefficient) & increment) == 0) {
        coefficient += coefficient < 0 ? -increment : increment;
    }
}

unsigned refine_run(Scan& scan, Block& block, unsigned coefficient, int run) {
    while (coefficient <= scan.end) {
        auto& value = block[kZigzag[coefficient]];
        if (value != 0) {
            refine_coefficient(scan, value);
        } else if (--run < 0) {
            break;
        }
        ++coefficient;
    }
    return coefficient;
}

int refinement_value(Scan& scan, unsigned size) {
    if (size > 1) {
        fail("invalid AC refinement magnitude");
    }
    if (size == 0) {
        return 0;
    }
    return (bits(scan, 1) != 0 ? 1 : -1) * (1 << scan.bit);
}

unsigned new_refinement(Scan& scan, Component& component, Block& block, unsigned start) {
    auto coefficient = start;
    while (coefficient <= scan.end) {
        const auto code = symbol(scan, scan.decoder.ac_tables[component.ac_table]);
        const auto run = code >> 4;
        const auto size = code & 15;
        if (begin_end_run(scan, code, 0)) {
            break;
        }
        const auto value = refinement_value(scan, size);
        coefficient = refine_run(scan, block, coefficient, static_cast<int>(run));
        if (coefficient > scan.end) {
            fail("AC refinement run exceeds spectral band");
        }
        if (value != 0) {
            block[kZigzag[coefficient]] = value;
        }
        ++coefficient;
    }
    return coefficient;
}

void refine_ac(Scan& scan, Component& component, Block& block) {
    auto coefficient = scan.start;
    if (scan.end_run == 0) {
        coefficient = new_refinement(scan, component, block, coefficient);
    }
    if (scan.end_run == 0) {
        return;
    }
    for (; coefficient <= scan.end; ++coefficient) {
        auto& value = block[kZigzag[coefficient]];
        if (value != 0) {
            refine_coefficient(scan, value);
        }
    }
    --scan.end_run;
}

} // namespace

void decode_progressive(Scan& scan, Component& component, Block& block) {
    if (scan.start == 0) {
        dc_coefficient(scan, component, block);
    } else if (scan.previous_bit == 0) {
        first_ac(scan, component, block);
    } else {
        refine_ac(scan, component, block);
    }
}

} // namespace engine::world::storage::jpeg
