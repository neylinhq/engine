#ifndef ENGINE_WORLD_MODEL_SRC_HELPERS_HPP
#define ENGINE_WORLD_MODEL_SRC_HELPERS_HPP

#include <engine/world/model/types.hpp>

#include <array>
#include <deque>
#include <span>
#include <unordered_map>

namespace engine::world::model {

using BlockWords = std::array<std::uint32_t, 32>;
using DenseChunk = std::array<std::uint16_t, 4096>;

struct PackedChunk {
    std::uint32_t uniform = 0x80000000U;
    // Only mixed chunks use these arrays; pack_chunk initializes both before use.
    std::array<std::uint32_t, 64> kinds;
    std::array<BlockWords, 64> blocks;
};

struct BlockHash {
    [[nodiscard]] std::size_t operator()(const BlockWords& words) const;
};

struct BlockSlot {
    std::uint32_t offset = 0;
    std::uint32_t references = 0;
};

struct BlockPool {
    std::unordered_map<BlockWords, BlockSlot, BlockHash> index;
    std::deque<std::uint32_t> free_voxels;
    std::deque<std::uint32_t> free_blocks;
    std::uint64_t references = 0;
    bool raw_updates = false;
    std::vector<std::uint32_t> changed_voxels;
    std::vector<std::uint32_t> changed_blocks;
};

struct BoundWork {
    std::deque<std::size_t> queue;
    std::vector<std::uint8_t> scheduled;
};

struct ChunkBounds {
    std::span<std::uint32_t> chunks;
    std::span<const std::uint32_t> entities;
    Vec3i size;
};

[[nodiscard]] std::size_t volume(Vec3i size);
[[nodiscard]] std::size_t flatten(Vec3i position, Vec3i size);
[[nodiscard]] Vec3i unflatten(std::size_t index, Vec3i size);
[[nodiscard]] bool inside(Vec3i position, Vec3i size);
[[nodiscard]] std::uint32_t checked_pointer(std::size_t value);
[[nodiscard]] std::array<std::uint32_t, 4> pack_material(const Material& material);
[[nodiscard]] DenseChunk decode_chunk(const Model& model, std::size_t index);
void pack_chunk(const DenseChunk& dense, bool raw, PackedChunk& packed);
[[nodiscard]] std::uint32_t store_chunk(Model& model, const PackedChunk& packed, BlockPool& pool,
                                        std::uint32_t previous);
[[nodiscard]] std::uint32_t encode_chunk(Model& model, const DenseChunk& dense, BlockPool& pool,
                                         std::uint32_t previous = 0);
void release_chunk(Model& model, std::uint32_t chunk, BlockPool& pool);
void initialize_pool(const Model& model, BlockPool& pool);
[[nodiscard]] double block_compression(const BlockPool& pool);
void unique_indices(std::vector<std::uint32_t>& indices);
void bounds4(std::span<std::uint32_t, 64> cells, unsigned state_shift);
void chunk_bounds(std::span<std::uint32_t> chunks, Vec3i size);
void invalidate_bounds(ChunkBounds bounds, std::span<const std::size_t> changed, BoundWork& work);
[[nodiscard]] bool advance_bounds(ChunkBounds bounds, BoundWork& work, std::size_t budget);
void model_bounds(Model& model);
[[nodiscard]] Model generate_model(Model source, Vec3i actual_size, Vec3i padded_size);
[[nodiscard]] std::vector<std::uint32_t> entity_voxels(const Model& model);
[[nodiscard]] Vec3 rotate(Vec3 point, const std::array<float, 4>& quaternion);
[[nodiscard]] std::array<std::uint32_t, 2> pack_quaternion(std::array<float, 4> quaternion);

} // namespace engine::world::model

#endif
