#include "helpers.hpp"

#include <algorithm>

namespace engine::world::model {
namespace {

struct BoundsField {
    std::span<std::uint32_t> cells;
    IVec3 size;
    unsigned bits = 2;
    unsigned state_shift = 30;
};

constexpr std::array<unsigned, 6> masks{0x3D, 0x3E, 0x37, 0x3B, 0x1F, 0x2F};

unsigned matching(std::uint32_t neighbor, std::uint32_t current, unsigned bits) {
    unsigned result = 0;
    const auto mask = (1U << bits) - 1;
    for (unsigned axis = 0; axis < 6; ++axis) {
        const auto shift = axis * bits;
        if (((neighbor >> shift) & mask) >= ((current >> shift) & mask)) {
            result |= 1U << axis;
        }
    }
    return result;
}

void grow(BoundsField field, std::size_t index, std::size_t neighbor, unsigned direction) {
    auto& word = field.cells[index];
    const auto next = field.cells[neighbor];
    const auto shift = direction * field.bits;
    const auto maximum = (1U << field.bits) - 1;
    if ((next >> field.state_shift) != 0 || ((word >> shift) & maximum) == maximum) {
        return;
    }
    if ((matching(next, word, field.bits) & masks[direction]) == masks[direction]) {
        word += 1U << shift;
    }
}

void sweep(BoundsField field, unsigned axis) {
    const std::array<int, 3> sizes{field.size.x, field.size.y, field.size.z};
    const std::array<std::size_t, 3> stride{1, static_cast<std::size_t>(field.size.x),
                                            static_cast<std::size_t>(field.size.x) * field.size.y};
    for (std::size_t index = 0; index < field.cells.size(); ++index) {
        if ((field.cells[index] >> field.state_shift) != 0) {
            continue;
        }
        const auto position = (index / stride[axis]) % static_cast<unsigned>(sizes[axis]);
        if (position > 0) {
            grow(field, index, index - stride[axis], axis * 2);
        }
        if (position + 1 < static_cast<unsigned>(sizes[axis])) {
            grow(field, index, index + stride[axis], axis * 2 + 1);
        }
    }
}

void calculate(BoundsField field, unsigned rounds) {
    for (auto& word : field.cells) {
        if ((word >> field.state_shift) == 0) {
            word = 0;
        }
    }
    for (unsigned round = 0; round < rounds; ++round) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            sweep(field, axis);
        }
    }
}

void voxel_block_bounds(std::span<std::uint32_t, 32> words) {
    std::array<std::uint32_t, 64> cells{};
    for (std::size_t index = 0; index < 32; ++index) {
        cells[index * 2] = words[index] & 0xFFFFU;
        cells[index * 2 + 1] = words[index] >> 16;
    }
    bounds4(cells, 15);
    for (std::size_t index = 0; index < 32; ++index) {
        words[index] = cells[index * 2] | (cells[index * 2 + 1] << 16);
    }
}

} // namespace

void bounds4(std::span<std::uint32_t, 64> cells, unsigned state_shift) {
    calculate({cells, {4, 4, 4}, 2, state_shift}, 3);
}

void chunk_bounds(std::span<std::uint32_t> chunks, IVec3 size) {
    calculate({chunks, size, 5, 30}, 31);
}

void model_bounds(Model& model) {
    for (std::size_t index = 0; index + 32 <= model.voxels.size(); index += 32) {
        voxel_block_bounds(std::span<std::uint32_t, 32>{model.voxels.data() + index, 32});
    }
    for (std::size_t index = 0; index + 64 <= model.blocks.size(); index += 64) {
        bounds4(std::span<std::uint32_t, 64>{model.blocks.data() + index, 64}, 30);
    }
    chunk_bounds(model.chunks, chunk_size(model.size));
}

std::vector<std::uint32_t> entity_voxels(const Model& model) {
    std::vector<std::uint32_t> result(volume(model.size));
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto material = voxel_at(model, unflatten(index, model.size));
        result[index] = material == 0 ? 0 : (0x80000000U | material);
    }
    calculate({result, model.size, 5, 31}, 31);
    return result;
}

} // namespace engine::world::model
