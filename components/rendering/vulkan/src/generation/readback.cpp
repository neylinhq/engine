#include "state.hpp"

#include "../frame/values.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {

void copy_words(WorldCompute& compute, const char* name, std::vector<std::uint32_t>& words) {
    const auto data = compute.read(name, 0, words.size() * sizeof(std::uint32_t));
    std::memcpy(words.data(), data.data(), data.size());
}

void copy_chunks(GenerationState& state, std::vector<std::uint32_t>& chunks) {
    auto& compute = state.compute;
    const auto limit =
        std::uint64_t(compute.device.properties.limits.maxComputeWorkGroupCount[0]) * 64;
    const auto batch = static_cast<std::uint32_t>(std::min(std::uint64_t{16 * 1024 * 1024}, limit));
    const auto count = static_cast<std::uint32_t>(chunks.size());
    compute.allocate("gpuCpuSyncBuffer", std::min(count, batch) * vk::DeviceSize{4});
    UniformValues values;
    set_value(values, "sizeInChunksX", state.chunks.x);
    set_value(values, "sizeInChunksY", state.chunks.y);
    set_value(values, "sizeInChunksZ", state.chunks.z);
    set_value(values, "copyMaxCount", count);
    for (std::uint32_t offset = 0; offset < count;) {
        const auto amount = std::min(count - offset, batch);
        set_value(values, "copyOffset", offset);
        compute.run("world_data_chunkCalc_chunkCopyToCpu", {(amount + 63) / 64, 1, 1}, values);
        const auto data = compute.read("gpuCpuSyncBuffer", 0, amount * vk::DeviceSize{4});
        std::memcpy(chunks.data() + offset, data.data(), data.size());
        offset += amount;
    }
    compute.buffers.erase("gpuCpuSyncBuffer");
}

void validate_chunk_graph(const GenerationResult& result) {
    std::vector<bool> referenced(result.block_words / 64);
    std::size_t count = 1;
    for (const auto chunk : result.model.chunks) {
        if ((chunk >> 30) != 2) {
            continue;
        }
        const auto pointer = chunk & 0x3FFFFFFFU;
        if (pointer < 64 || pointer % 64 != 0 || pointer + std::uint64_t{64} > result.block_words ||
            referenced[pointer / 64]) {
            throw std::runtime_error("GPU chunk allocation graph is invalid");
        }
        referenced[pointer / 64] = true;
        ++count;
    }
    if (count != referenced.size()) {
        throw std::runtime_error("GPU chunk allocation graph contains an unreferenced range");
    }
}

std::vector<std::uint32_t> graph_references(const GenerationResult& result) {
    validate_chunk_graph(result);
    std::vector<std::uint32_t> references(result.logical_voxels / 64);
    for (const auto block : result.model.blocks) {
        if ((block >> 30) != 2) {
            continue;
        }
        const auto pointer = block & 0x3FFFFFFFU;
        if (pointer < 32 || pointer % 32 != 0 ||
            pointer + std::uint64_t{32} > result.model.voxels.size()) {
            throw std::runtime_error(
                "GPU hash insertion returned an invalid or unpublished pointer");
        }
        ++references[pointer / 32];
    }
    return references;
}

double compression(const GenerationResult& result) {
    auto graph = graph_references(result);
    std::uint64_t unique = 0;
    std::uint64_t references = 0;
    for (const auto& entry : result.hash) {
        if (entry[0] == 0) {
            continue;
        }
        if ((entry[0] & 31U) != 0 || (entry[0] & 0x80000000U) != 0 ||
            entry[0] + std::uint64_t{32} > result.model.voxels.size() || entry[1] == 0) {
            throw std::runtime_error("GPU block hash publication or pointer is invalid");
        }
        if (graph[entry[0] / 32] != entry[1]) {
            throw std::runtime_error("GPU hash use count differs from the generated block graph");
        }
        graph[entry[0] / 32] = 0;
        ++unique;
        references += entry[1];
    }
    if (unique != (result.logical_voxels - 64) / 64) {
        throw std::runtime_error("GPU block hash table lost an allocated voxel range");
    }
    return unique == 0 ? 1 : static_cast<double>(references) / static_cast<double>(unique);
}

std::uint32_t block_hash(const world::model::Model& model, std::uint32_t offset) {
    std::uint32_t hash = 1;
    for (std::uint32_t index = 0; index < 32; ++index) {
        const auto word = model.voxels[offset + index];
        hash = hash * 31U + (word & 0x7FFFU);
        hash = hash * 31U + ((word >> 16) & 0x7FFFU);
    }
    return hash;
}

void validate_hash_chain(const GenerationResult& result, std::size_t index) {
    const auto& entry = result.hash[index];
    auto slot = static_cast<std::size_t>(entry[2]) & (result.hash.size() - 1);
    const auto* words = result.model.voxels.data() + entry[0];
    while (slot != index) {
        const auto& previous = result.hash[slot];
        if (previous[0] == 0) {
            throw std::runtime_error("GPU hash record is outside its linear probe chain");
        }
        if (previous[2] == entry[2] &&
            std::equal(words, words + 32, result.model.voxels.data() + previous[0])) {
            throw std::runtime_error("GPU hash allocated duplicate raw blocks");
        }
        slot = (slot + 1) & (result.hash.size() - 1);
    }
}

void validate_hash_content(const GenerationResult& result) {
    for (std::size_t index = 0; index < result.hash.size(); ++index) {
        const auto& entry = result.hash[index];
        if (entry[0] == 0) {
            continue;
        }
        if (block_hash(result.model, entry[0]) != entry[2]) {
            throw std::runtime_error("GPU hash record differs from its raw voxel content");
        }
        validate_hash_chain(result, index);
    }
}

void bounds(WorldCompute& compute, const char* name, std::uint32_t groups) {
    const auto maximum = compute.device.properties.limits.maxComputeWorkGroupCount[0];
    UniformValues values;
    for (std::uint32_t offset = 0; offset < groups;) {
        const auto count = std::min(maximum, groups - offset);
        set_value(values, "boundsGroupOffset", offset);
        compute.run(name, {count, 1, 1}, values);
        offset += count;
    }
}

} // namespace

GenerationResult generation_readback(GenerationState& state) {
    GenerationResult result;
    result.model.name = state.source.name;
    result.model.size = state.dimensions;
    result.model.materials = state.source.materials;
    result.model.material_context = state.source.material_context;
    result.actual_size = state.requested;
    result.logical_voxels = state.counts[0];
    result.block_words = state.counts[1];
    result.segments = state.segments;
    result.model.chunks.resize(std::size_t(state.chunks.x) * state.chunks.y * state.chunks.z);
    result.model.blocks.resize(state.counts[1]);
    result.model.voxels.resize(state.counts[0] / 2);
    copy_chunks(state, result.model.chunks);
    copy_words(state.compute, "blocks", result.model.blocks);
    copy_words(state.compute, "voxels", result.model.voxels);
    result.hash.resize(state.map_slots);
    const auto hashes = state.compute.read("hashMap", 0, result.hash.size() * 12);
    std::memcpy(result.hash.data(), hashes.data(), hashes.size());
    result.compression = compression(result);
    validate_hash_content(result);
    world::model::validate(result.model);
    return result;
}

void generation_bounds(GenerationState& state) {
    bounds(state.compute, "world_data_chunkCalc_computeVoxelBounds", state.counts[0] / 64);
    bounds(state.compute, "world_data_chunkCalc_computeBlockBounds", state.counts[1] / 64);
    state.compute.synchronize();
    for (const auto* name :
         {"modelDataChunk", "modelDataBlock", "modelDataVoxel", "chunkData", "segmentVoxelBuffer",
          "hashMap", "blockVoxelCount", "oldMap", "newMap", "resultHash"}) {
        state.compute.buffers.erase(name);
    }
}

} // namespace engine::rendering::vulkan
