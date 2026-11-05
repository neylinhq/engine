#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine::world::model {
namespace {

void initialize_gpu_pool(const GeneratedWorld& generated, BlockPool& pool) {
    pool.raw_updates = true;
    for (const auto& entry : generated.hash) {
        const auto offset = entry[0];
        const auto references = entry[1];
        if (offset == 0) {
            continue;
        }
        if (references == 0 || offset % 32 != 0 ||
            static_cast<std::size_t>(offset) + 32 > generated.model.voxels.size()) {
            throw std::runtime_error("Generated voxel hash contains an invalid slot");
        }
        BlockWords words{};
        std::copy_n(generated.model.voxels.begin() + offset, 32, words.begin());
        if (BlockHash{}(words) != entry[2]) {
            throw std::runtime_error("Generated voxel hash disagrees with its raw voxel block");
        }
        if (!pool.index.emplace(words, BlockSlot{offset, references}).second) {
            throw std::runtime_error("Generated voxel hash contains duplicate blocks");
        }
        pool.references += references;
    }
}

void validate_generated(const GeneratedWorld& generated, std::uint64_t context) {
    validate(generated.model);
    static_cast<void>(chunk_size(generated.actual_size));
    if (generated.model.material_context != context) {
        throw std::runtime_error("Generated world belongs to another material registry");
    }
    const auto size = generated.model.size;
    if (generated.actual_size.x > size.x || generated.actual_size.y > size.y ||
        generated.actual_size.z > size.z || generated.logical_voxels < 64 ||
        generated.logical_voxels % 64 != 0 ||
        generated.logical_voxels / 2 != generated.model.voxels.size() ||
        generated.block_words < 64 || generated.block_words % 64 != 0 ||
        generated.block_words != generated.model.blocks.size() ||
        !std::isfinite(generated.compression) || generated.compression < 1) {
        throw std::runtime_error("GPU generation returned inconsistent world metadata");
    }
}

} // namespace

void unique_indices(std::vector<std::uint32_t>& indices) {
    std::ranges::sort(indices);
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

void World::adopt_generated(GeneratedWorld generated) {
    validate_generated(generated, state_->material_context);
    auto next = std::make_unique<State>();
    next->material_context = state_->material_context;
    next->next_anonymous_id = state_->next_anonymous_id;
    next->actual_size = generated.actual_size;
    next->compression_factor = generated.compression;
    next->gpu_authoritative = true;
    initialize_gpu_pool(generated, next->pool);
    next->model = std::move(generated.model);
    for (const auto& material : next->model.materials) {
        next->packed_materials.push_back(pack_material(material));
    }
    next->entity_gpu.chunks.resize(next->model.chunks.size());
    next->revision = state_->revision + 1;
    next->chunk_revision = state_->chunk_revision + 1;
    next->geometry_revision = state_->geometry_revision + 1;
    next->material_revision = state_->material_revision + 1;
    next->entity_gpu.revision = state_->entity_gpu.revision + 1;
    next->entity_gpu.voxel_revision = state_->entity_gpu.voxel_revision + 1;
    next->models = std::move(state_->models);
    state_ = std::move(next);
}

void World::acknowledge_changes() {
    state_->changed_chunks.clear();
    state_->pool.changed_blocks.clear();
    state_->pool.changed_voxels.clear();
    state_->invalidated_chunks.clear();
    state_->entity_gpu.changed_chunks.clear();
}

void World::State::invalidate(std::span<const std::size_t> changed) {
    if (gpu_authoritative) {
        for (auto index : changed) {
            invalidated_chunks.push_back(static_cast<std::uint32_t>(index));
        }
        unique_indices(invalidated_chunks);
    } else {
        invalidate_bounds({model.chunks, entity_gpu.chunks, chunk_size(model.size)}, changed,
                          bounds);
    }
}

} // namespace engine::world::model
