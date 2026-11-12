#include "../state.hpp"

#include <algorithm>
#include <stdexcept>

namespace engine::world::model {

std::array<std::uint16_t, 4096> World::chunk_voxels(Vec3i chunk) const {
    const auto dimensions = chunk_size(size());
    if (!inside(chunk, dimensions)) {
        throw std::out_of_range("Chunk is outside the world");
    }
    const auto index = flatten(chunk, dimensions);
    const auto found = state_->edits.find(index);
    if (found == state_->edits.end()) {
        return decode_chunk(state_->model, index);
    }
    if (found->second.voxels) {
        return *found->second.voxels;
    }
    DenseChunk result;
    result.fill(found->second.material);
    return result;
}

void World::set_chunk_voxels(Vec3i chunk, std::span<const std::uint16_t, 4096> voxels) {
    auto copy = std::make_unique<DenseChunk>();
    std::ranges::copy(voxels, copy->begin());
    adopt_chunk_voxels(chunk, std::move(copy));
}

void World::adopt_chunk_voxels(Vec3i chunk, std::unique_ptr<DenseChunk> voxels) {
    const auto dimensions = chunk_size(size());
    if (!inside(chunk, dimensions)) {
        throw std::out_of_range("Chunk is outside the world");
    }
    if (!voxels || std::ranges::max(*voxels) >= state_->model.materials.size()) {
        throw std::runtime_error("Selected material is outside palette");
    }
    auto& destination = state_->edits[flatten(chunk, dimensions)];
    destination.voxels = std::move(voxels);
}

void World::fill_chunk(Vec3i chunk, std::uint16_t material) {
    const auto dimensions = chunk_size(size());
    if (!inside(chunk, dimensions)) {
        throw std::out_of_range("Chunk is outside the world");
    }
    if (material >= state_->model.materials.size()) {
        throw std::runtime_error("Selected material is outside palette");
    }
    const auto index = flatten(chunk, dimensions);
    const auto found = state_->edits.find(index);
    const auto word = state_->model.chunks[index];
    const auto uniform = material == 0 ? 0U : 0x40000000U | material;
    if (found == state_->edits.end() && (word == uniform || (material == 0 && (word >> 30) == 0))) {
        return;
    }
    state_->edits[index] = {nullptr, material};
}

} // namespace engine::world::model
