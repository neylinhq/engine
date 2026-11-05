#include "helpers.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace engine::world::model {
namespace {

std::uint16_t half(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = (bits >> 16) & 0x8000U;
    const auto exponent = static_cast<int>((bits >> 23) & 255U) - 127 + 15;
    const auto mantissa = bits & 0x7FFFFFU;
    if (exponent >= 31) {
        const auto nan = ((bits >> 23) & 255U) == 255U && mantissa != 0;
        return static_cast<std::uint16_t>(sign | 0x7C00U | (nan ? 0x200U : 0));
    }
    if (exponent < -10) {
        return static_cast<std::uint16_t>(sign);
    }
    if (exponent <= 0) {
        const auto shift = static_cast<unsigned>(14 - exponent);
        const auto normalized = mantissa | 0x800000U;
        const auto rounded =
            (normalized + ((1U << (shift - 1)) - 1) + ((normalized >> shift) & 1U)) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    const auto rounded = mantissa + 0xFFFU + ((mantissa >> 13) & 1U);
    return static_cast<std::uint16_t>(
        sign | ((static_cast<std::uint32_t>(exponent) << 10) + (rounded >> 13)));
}

std::uint32_t pair(double low, double high) {
    return half(static_cast<float>(low)) | (std::uint32_t{half(static_cast<float>(high))} << 16);
}

BlockWords pack_block(const DenseChunk& dense, IVec3 block, bool raw) {
    std::array<std::uint32_t, 64> cells{};
    const auto origin = block.x * 4 + block.y * 64 + block.z * 1024;
    for (std::size_t index = 0; index < 64; ++index) {
        const auto local = (index & 3) + ((index & 12) << 2) + ((index & 48) << 4);
        const auto type = dense[origin + local];
        cells[index] = type == 0 ? 0U : (0x8000U | type);
    }
    if (!raw) {
        bounds4(cells, 15);
    }
    BlockWords result{};
    for (std::size_t index = 0; index < 32; ++index) {
        result[index] = cells[index * 2] | (cells[index * 2 + 1] << 16);
    }
    return result;
}

std::uint32_t store_block(Model& model, BlockPool& pool, const BlockWords& words) {
    auto [slot, inserted] = pool.index.try_emplace(words, BlockSlot{});
    if (!inserted) {
        ++pool.references;
        ++slot->second.references;
        return 0x80000000U | slot->second.offset;
    }
    try {
        auto offset = checked_pointer(model.voxels.size());
        if (!pool.free_voxels.empty()) {
            offset = pool.free_voxels.front();
            pool.free_voxels.pop_front();
        } else {
            model.voxels.resize(model.voxels.size() + 32);
        }
        std::copy(words.begin(), words.end(), model.voxels.begin() + offset);
        slot->second = {offset, 1};
        if (pool.raw_updates) {
            pool.changed_voxels.push_back(offset);
        }
    } catch (...) {
        pool.index.erase(slot);
        throw;
    }
    ++pool.references;
    return 0x80000000U | slot->second.offset;
}

std::uint32_t block_kind(const BlockWords& words) {
    const auto first = words.front() & 0xFFFFU;
    const auto solid = (first & 0x8000U) != 0;
    const auto same = std::ranges::all_of(
        words, [first](auto word) { return (word & 0xFFFFU) == first && (word >> 16) == first; });
    const auto empty =
        std::ranges::all_of(words, [](auto word) { return (word & 0x80008000U) == 0; });
    if (empty) {
        return 0;
    }
    if (same && solid) {
        return 0x40000000U | (first & 0x7FFFU);
    }
    return 0x80000000U;
}

void release_block(const Model& model, std::uint32_t block, BlockPool& pool) {
    if ((block >> 30) != 2) {
        return;
    }
    BlockWords words{};
    const auto start = block & 0x3FFFFFFFU;
    std::copy_n(model.voxels.begin() + start, 32, words.begin());
    const auto found = pool.index.find(words);
    if (found == pool.index.end() || found->second.references == 0) {
        throw std::runtime_error("Voxel block is missing from the CPU edit hash");
    }
    --pool.references;
    if (--found->second.references == 0) {
        pool.free_voxels.push_back(found->second.offset);
        pool.index.erase(found);
    }
}

std::uint32_t block_slot(Model& model, BlockPool& pool, std::uint32_t previous) {
    if (pool.raw_updates && (previous >> 30) == 2) {
        return previous & 0x3FFFFFFFU;
    }
    if (!pool.free_blocks.empty()) {
        const auto offset = pool.free_blocks.front();
        pool.free_blocks.pop_front();
        return offset;
    }
    const auto offset = checked_pointer(model.blocks.size());
    model.blocks.resize(model.blocks.size() + 64);
    return offset;
}

} // namespace

std::size_t BlockHash::operator()(const BlockWords& words) const {
    std::uint32_t result = 1;
    for (auto word : words) {
        result = result * 31 + (word & 0x7FFFU);
        result = result * 31 + ((word >> 16) & 0x7FFFU);
    }
    return result;
}

std::array<std::uint32_t, 4> pack_material(const Material& material) {
    if (!foundation::geometry::finite(material.color_base) ||
        !foundation::geometry::finite(material.color_layer) || !std::isfinite(material.roughness) ||
        static_cast<unsigned>(material.base) > 3 || static_cast<unsigned>(material.layer) == 1 ||
        static_cast<unsigned>(material.layer) > 3) {
        throw std::runtime_error("Invalid material values");
    }
    return {static_cast<std::uint32_t>(material.base) |
                (static_cast<std::uint32_t>(material.layer) << 2) |
                (std::uint32_t{half(material.roughness)} << 16),
            pair(material.color_base.x, material.color_base.y),
            pair(material.color_base.z, material.color_layer.x),
            pair(material.color_layer.y, material.color_layer.z)};
}

DenseChunk decode_chunk(const Model& model, std::size_t index) {
    DenseChunk result{};
    const auto word = model.chunks[index];
    if ((word >> 30) != 2) {
        if ((word >> 30) == 1) {
            result.fill(static_cast<std::uint16_t>(word & 0x7FFFU));
        }
        return result;
    }
    const auto offset = word & 0x3FFFFFFFU;
    for (unsigned block = 0; block < 64; ++block) {
        const auto encoded = model.blocks[offset + block];
        const auto kind = encoded >> 30;
        const auto origin = ((block & 3) << 2) + ((block & 12) << 4) + ((block & 48) << 6);
        for (unsigned voxel = 0; voxel < 64; ++voxel) {
            const auto packed = kind == 2 ? model.voxels[(encoded & 0x3FFFFFFFU) + voxel / 2] : 0U;
            const auto cell = (packed >> ((voxel & 1) * 16)) & 0xFFFFU;
            const auto material = kind == 1          ? encoded & 0x7FFFU
                                  : (cell & 0x8000U) ? cell & 0x7FFFU
                                                     : 0U;
            const auto local = (voxel & 3) + ((voxel & 12) << 2) + ((voxel & 48) << 4);
            result[origin + local] = static_cast<std::uint16_t>(material);
        }
    }
    return result;
}

std::uint32_t encode_chunk(Model& model, const DenseChunk& dense, BlockPool& pool,
                           std::uint32_t previous) {
    PackedChunk packed;
    pack_chunk(dense, pool.raw_updates, packed);
    return store_chunk(model, packed, pool, previous);
}

void pack_chunk(const DenseChunk& dense, bool raw, PackedChunk& packed) {
    const auto first = dense.front();
    if (std::ranges::all_of(dense, [first](auto value) { return value == first; })) {
        packed.uniform = first == 0 ? 0U : 0x40000000U | first;
        return;
    }
    packed.uniform = 0x80000000U;
    for (std::size_t index = 0; index < 64; ++index) {
        const IVec3 block{static_cast<int>(index & 3), static_cast<int>((index >> 2) & 3),
                          static_cast<int>(index >> 4)};
        packed.blocks[index] = pack_block(dense, block, raw);
        packed.kinds[index] = block_kind(packed.blocks[index]);
    }
}

std::uint32_t store_chunk(Model& model, const PackedChunk& packed, BlockPool& pool,
                          std::uint32_t previous) {
    if ((packed.uniform >> 30) != 2) {
        if (pool.raw_updates) {
            release_chunk(model, previous, pool);
        }
        return packed.uniform;
    }
    std::array<std::uint32_t, 64> blocks{};
    for (std::size_t index = 0; index < 64; ++index) {
        blocks[index] = (packed.kinds[index] >> 30) == 2
                            ? store_block(model, pool, packed.blocks[index])
                            : packed.kinds[index];
    }
    if (pool.raw_updates && (previous >> 30) == 2) {
        for (std::size_t index = 0; index < 64; ++index) {
            release_block(model, model.blocks[(previous & 0x3FFFFFFFU) + index], pool);
        }
    }
    if (!pool.raw_updates) {
        bounds4(blocks, 30);
    }
    const auto offset = block_slot(model, pool, previous);
    std::copy(blocks.begin(), blocks.end(), model.blocks.begin() + offset);
    if (pool.raw_updates) {
        pool.changed_blocks.push_back(offset);
    }
    return offset | 0x80000000U;
}

void release_chunk(Model& model, std::uint32_t chunk, BlockPool& pool) {
    if ((chunk >> 30) != 2) {
        return;
    }
    const auto offset = chunk & 0x3FFFFFFFU;
    for (unsigned block = 0; block < 64; ++block) {
        release_block(model, model.blocks[offset + block], pool);
    }
    pool.free_blocks.push_back(offset);
}

void initialize_pool(const Model& model, BlockPool& pool) {
    pool = {};
    for (auto chunk : model.chunks) {
        if ((chunk >> 30) != 2) {
            continue;
        }
        for (unsigned index = 0; index < 64; ++index) {
            const auto block = model.blocks[(chunk & 0x3FFFFFFFU) + index];
            if ((block >> 30) != 2) {
                continue;
            }
            BlockWords words{};
            const auto offset = block & 0x3FFFFFFFU;
            std::copy_n(model.voxels.begin() + offset, 32, words.begin());
            auto& slot = pool.index[words];
            slot.offset = offset;
            ++slot.references;
            ++pool.references;
        }
    }
}

double block_compression(const BlockPool& pool) {
    return pool.index.empty() ? 1 : static_cast<double>(pool.references) / pool.index.size();
}

} // namespace engine::world::model
