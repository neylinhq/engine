#include "decoder.hpp"

#include <cstdlib>

namespace engine::world::storage::jpeg {
namespace {

void restart(Scan& scan) {
    scan.remaining_bits = 0;
    if (marker(scan.decoder) != 0xd0 + scan.restart_index) {
        fail("incorrect restart marker sequence");
    }
    scan.restart_index = (scan.restart_index + 1) & 7;
    scan.end_run = 0;
    for (const auto index : scan.components) {
        scan.decoder.components[index].predictor = 0;
    }
}

void interleaved_component(Scan& scan, Component& component, unsigned mcu) {
    const auto x = (mcu % scan.decoder.mcu_columns) * component.horizontal;
    const auto y = (mcu / scan.decoder.mcu_columns) * component.vertical;
    for (unsigned row = 0; row < component.vertical; ++row) {
        for (unsigned column = 0; column < component.horizontal; ++column) {
            auto& block = component.blocks[(y + row) * component.columns + x + column];
            decode_block(scan, component, block);
        }
    }
}

void decode_mcu(Scan& scan, unsigned mcu) {
    if (scan.components.size() == 1) {
        auto& component = scan.decoder.components[scan.components.front()];
        const auto x = mcu % component.actual_columns;
        const auto y = mcu / component.actual_columns;
        decode_block(scan, component, component.blocks[y * component.columns + x]);
        return;
    }
    for (const auto index : scan.components) {
        interleaved_component(scan, scan.decoder.components[index], mcu);
    }
}

void sequential_dc(Scan& scan, Component& component, Block& block) {
    const auto category = symbol(scan, scan.decoder.dc_tables[component.dc_table]);
    if (category > 11) {
        fail("invalid DC category");
    }
    component.predictor += amplitude(scan, category);
    if (std::abs(component.predictor) > 16777216) {
        fail("DC predictor overflow");
    }
    block[0] = component.predictor;
}

bool sequential_end(unsigned code) {
    if ((code & 15) != 0 || code == 0xf0) {
        return false;
    }
    if (code != 0) {
        fail("invalid sequential end-of-block symbol");
    }
    return true;
}

void sequential(Scan& scan, Component& component, Block& block) {
    sequential_dc(scan, component, block);
    unsigned coefficient = 1;
    while (coefficient < 64) {
        const auto code = symbol(scan, scan.decoder.ac_tables[component.ac_table]);
        const auto size = code & 15;
        if (sequential_end(code)) {
            break;
        }
        coefficient += size == 0 ? 16 : code >> 4;
        if (size > 10 || coefficient > 64 || (size != 0 && coefficient == 64)) {
            fail("invalid AC coefficient run");
        }
        if (size != 0) {
            block[kZigzag[coefficient++]] = amplitude(scan, size);
        }
    }
}

} // namespace

void decode_block(Scan& scan, Component& component, Block& block) {
    if (scan.decoder.progressive) {
        decode_progressive(scan, component, block);
    } else {
        sequential(scan, component, block);
    }
}

void decode_scan(Scan& scan) {
    const auto& component = scan.decoder.components[scan.components.front()];
    const auto count = scan.components.size() == 1
                           ? component.actual_columns * component.actual_rows
                           : scan.decoder.mcu_columns * scan.decoder.mcu_rows;
    for (unsigned mcu = 0; mcu < count; ++mcu) {
        if (mcu != 0 && scan.decoder.restart_interval != 0 &&
            mcu % scan.decoder.restart_interval == 0) {
            restart(scan);
        }
        decode_mcu(scan, mcu);
    }
    scan.remaining_bits = 0;
}

} // namespace engine::world::storage::jpeg
