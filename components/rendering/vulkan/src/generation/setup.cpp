#include "state.hpp"

#include "../world_updates/support.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {

world::model::IVec3 segment_counts(world::model::IVec3 requested, int side) {
    if (requested.x < 1 || requested.y < 1 || requested.z < 1) {
        throw std::invalid_argument("GPU generation requires positive world dimensions");
    }
    const auto maximum = std::numeric_limits<int>::max() - side;
    if (requested.x > maximum || requested.y > maximum || requested.z > maximum) {
        throw std::invalid_argument("GPU world dimensions exceed integer range");
    }
    return {(requested.x + side - 1) / side, (requested.y + side - 1) / side,
            (requested.z + side - 1) / side};
}

std::uint32_t physical_chunks(WorldCompute& compute, std::size_t source_words, int logical_groups) {
    const auto maximum = compute.device.properties.limits.maxStorageBufferRange;
    const auto logical_chunks = static_cast<std::uint32_t>(logical_groups * 4);
    const auto source_bytes = std::uint64_t(source_words) * 4;
    for (auto chunks = logical_chunks; chunks > 0; --chunks) {
        const auto side = chunks * 16U;
        const auto raw_bytes = std::uint64_t(side) * side * side * 2;
        if (logical_chunks % chunks == 0 && raw_bytes + source_bytes + 128 <= maximum) {
            return chunks;
        }
    }
    throw std::runtime_error("GPU storage buffer limit cannot hold a generation segment");
}

void model_buffers(GenerationState& state) {
    for (const auto& [name, words] :
         {std::pair{"modelDataChunk", std::span(state.source.chunks)},
          std::pair{"modelDataBlock", std::span(state.source.blocks)},
          std::pair{"modelDataVoxel", std::span(state.source.voxels)}}) {
        state.compute.allocate(name, std::max<std::size_t>(4, words.size_bytes()));
        state.compute.upload(name, std::as_bytes(words));
    }
}

void chunk_image(GenerationState& state) {
    const auto maximum = state.compute.device.properties.limits.maxImageDimension3D;
    const vk::Extent3D extent(static_cast<std::uint32_t>(state.chunks.x),
                              static_cast<std::uint32_t>(state.chunks.y),
                              static_cast<std::uint32_t>(state.chunks.z));
    if (extent.width > maximum || extent.height > maximum || extent.depth > maximum) {
        throw std::runtime_error("GPU generation chunk volume exceeds device limits");
    }
    state.compute.chunks = make_chunk_images(state.compute.device, extent);
    const auto command = state.compute.command();
    for (const auto& image : {state.compute.chunks.geometry, state.compute.chunks.entities}) {
        if (image) {
            image_barrier(command, *image->handle, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eGeneral);
        }
    }
}

} // namespace

GenerationState generation_state(WorldCompute& compute, const world::model::Model& source,
                                 world::model::IVec3 requested, int segment_groups) {
    world::model::validate(source);
    if (segment_groups < 1 || segment_groups > 8) {
        throw std::invalid_argument("Generation group size must be in 1..8");
    }
    GenerationState state{compute, source, requested};
    const auto side = segment_groups * 64;
    state.segment_counts = segment_counts(requested, side);
    state.dimensions = {state.segment_counts.x * side, state.segment_counts.y * side,
                        state.segment_counts.z * side};
    state.chunks = world::model::chunk_size(state.dimensions);
    const auto chunk_count = std::uint64_t(state.chunks.x) * state.chunks.y * state.chunks.z;
    if (chunk_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("GPU chunk count exceeds uint32 indexing");
    }
    validate_aadf_capacity(compute.device, state.chunks);
    state.segment_chunks = physical_chunks(compute, source.voxels.size(), segment_groups);
    const auto physical_side = state.segment_chunks * 16;
    state.segment_counts = segment_counts(state.dimensions, static_cast<int>(physical_side));
    const auto volume = physical_side * physical_side * physical_side;
    state.raw_voxel_words = volume / 2;
    state.segment_block_words = volume / 64;
    state.map_slots = std::bit_ceil(state.segment_block_words * 4);
    if (state.raw_voxel_words > 0x80000000U || state.map_slots > 0x40000000U) {
        throw std::runtime_error("GPU generation marker or hash slot exceeds its payload bits");
    }
    state.coefficients.back() = 1;
    for (std::size_t index = 64; index != 0; --index) {
        state.coefficients[index - 1] = 31U * state.coefficients[index];
    }
    return state;
}

void generation_allocate(GenerationState& state) {
    auto& compute = state.compute;
    compute.synchronize();
    model_buffers(state);
    chunk_image(state);
    compute.allocate("segmentVoxelBuffer", vk::DeviceSize(state.raw_voxel_words) * 4);
    compute.buffers["chunkData"] = compute.buffers.at("segmentVoxelBuffer");
    compute.allocate("blocks", vk::DeviceSize(state.segment_block_words + 64) * 4);
    compute.allocate("voxels", vk::DeviceSize(state.raw_voxel_words + 32) * 4);
    compute.allocate("hashMap", vk::DeviceSize(state.map_slots) * 12);
    compute.allocate("blockVoxelCount", sizeof(state.counts));
    compute.upload("blockVoxelCount", std::as_bytes(std::span(state.counts)));
}

} // namespace engine::rendering::vulkan
