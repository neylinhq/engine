#include "state.hpp"

#include "../frame/values.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {

UniformValues generator_values(const GenerationState& state, world::model::Vec3i offset) {
    UniformValues values;
    const auto size = world::model::chunk_size(state.source.size);
    set_value(values, "modelSizeInChunksX", size.x);
    set_value(values, "modelSizeInChunksY", size.y);
    set_value(values, "modelSizeInChunksZ", size.z);
    set_value(values, "groupOffsetInChunksX", offset.x);
    set_value(values, "groupOffsetInChunksY", offset.y);
    set_value(values, "groupOffsetInChunksZ", offset.z);
    set_value(values, "groupSizeInChunksX", state.segment_chunks);
    set_value(values, "groupSizeInChunksY", state.segment_chunks);
    set_value(values, "sizeInVoxelsX", state.requested.x);
    set_value(values, "sizeInVoxelsY", state.requested.y);
    set_value(values, "sizeInVoxelsZ", state.requested.z);
    set_value(values, "worldSizeY", std::uint32_t{1});
    return values;
}

UniformValues hash_values(const GenerationState& state, world::model::Vec3i offset) {
    UniformValues values;
    set_value(values, "chunkOffsetX", offset.x);
    set_value(values, "chunkOffsetY", offset.y);
    set_value(values, "chunkOffsetZ", offset.z);
    set_value(values, "segmentSizeInChunks", state.segment_chunks);
    set_value(values, "hashMapSize", state.map_slots);
    set_value(values, "hashCoefficients", state.coefficients);
    return values;
}

void resize_map(GenerationState& state) {
    const auto required = std::uint64_t(state.counts[0] / 64) + state.segment_block_words * 2;
    if (required * 2 <= state.map_slots) {
        return;
    }
    std::uint64_t size = std::uint64_t(state.map_slots) * 2;
    while (size < required * 2) {
        size *= 2;
    }
    if (size > 0x40000000ULL) {
        throw std::runtime_error("GPU block hash table exceeds the temporary 30-bit slot field");
    }
    auto& compute = state.compute;
    compute.buffers["oldMap"] = compute.buffers.at("hashMap");
    compute.allocate("newMap", size * 12);
    UniformValues values;
    set_value(values, "oldSize", state.map_slots);
    set_value(values, "newSize", static_cast<std::uint32_t>(size));
    const auto limit =
        std::uint64_t(compute.device.properties.limits.maxComputeWorkGroupCount[0]) * 64;
    const auto batch = static_cast<std::uint32_t>(std::min(std::uint64_t(state.map_slots), limit));
    for (std::uint32_t offset = 0; offset < state.map_slots;) {
        const auto count = std::min(batch, state.map_slots - offset);
        set_value(values, "mapCopyOffset", offset);
        compute.run("world_data_mapCopy_copyMap", {(count + 63) / 64, 1, 1}, values);
        offset += count;
    }
    compute.synchronize();
    compute.buffers["hashMap"] = compute.buffers.at("newMap");
    compute.buffers.erase("oldMap");
    compute.buffers.erase("newMap");
    state.map_slots = static_cast<std::uint32_t>(size);
}

void finish_segment(GenerationState& state) {
    const auto bytes = state.compute.read("blockVoxelCount", 0, sizeof(state.counts));
    std::memcpy(state.counts.data(), bytes.data(), sizeof(state.counts));
    if (state.counts[0] < 64 || state.counts[0] % 64 || state.counts[1] < 64 ||
        state.counts[1] % 64) {
        throw std::runtime_error("GPU generation returned invalid allocation counters");
    }
    ++state.segments;
    resize_map(state);
    const auto voxel_words = vk::DeviceSize(state.counts[0] / 2) + state.raw_voxel_words;
    const auto block_words = vk::DeviceSize(state.counts[1]) + state.segment_block_words;
    state.compute.grow("voxels", voxel_words * 4);
    state.compute.grow("blocks", block_words * 4);
}

void segment(GenerationState& state, world::model::Vec3i index) {
    const auto side = static_cast<int>(state.segment_chunks);
    const world::model::Vec3i offset{index.x * side, index.y * side, index.z * side};
    const vk::Extent3D groups(state.segment_chunks, state.segment_chunks, state.segment_chunks);
    state.compute.run("world_generator_generatorModel_fillChunkDataWithModelData16", groups,
                      generator_values(state, offset));
    auto values = hash_values(state, offset);
    state.compute.run("world_data_chunkCalc_calcBlockFromRawData", groups, values);
    set_value(values, "resolvePointers", std::uint32_t{1});
    state.compute.run("world_data_chunkCalc_calcBlockFromRawData", groups, values);
    finish_segment(state);
}

} // namespace

void generation_segments(GenerationState& state) {
    for (int z = 0; z < state.segment_counts.z; ++z) {
        for (int y = 0; y < state.segment_counts.y; ++y) {
            for (int x = 0; x < state.segment_counts.x; ++x) {
                segment(state, {x, y, z});
            }
        }
    }
}

} // namespace engine::rendering::vulkan
