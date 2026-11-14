#include <engine/world/model/world.hpp>

#include "state.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace engine::world::model {
namespace {

Vec3i padded_size(Vec3i size, int groups) {
    static_cast<void>(chunk_size(size));
    if (groups < 1 || groups > 8) {
        throw std::runtime_error("World generation segment must be 1..8 groups");
    }
    const auto segment = groups * 64;
    return {((size.x + segment - 1) / segment) * segment,
            ((size.y + segment - 1) / segment) * segment,
            ((size.z + segment - 1) / segment) * segment};
}

} // namespace

World::World(unsigned worker_count)
    : state_(std::make_unique<State>()), worker_count_(worker_count) {
    static std::atomic<std::uint64_t> context = 1;
    state_->material_context = context.fetch_add(1);
    state_->model.materials.push_back({});
    state_->model.material_context = state_->material_context;
    state_->model.material_layout.emplace();
    state_->model.material_layout->slots.push_back({MaterialScope::persistent, {}});
    state_->packed_materials.push_back({});
}
World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

void World::replace(Model model, Vec3i requested_size, int segment_groups) {
    model = prepare_generation(std::move(model));
    auto next = std::make_unique<State>();
    next->actual_size = requested_size.x == 0 ? model.size : requested_size;
    const auto padded = padded_size(next->actual_size, segment_groups);
    if (padded.x > 16384 || padded.y > 16384 || padded.z > 16384) {
        throw std::runtime_error("World exceeds NAADF dimensions");
    }
    model = generate_model(std::move(model), next->actual_size, padded);
    model_bounds(model);
    next->adopt_scene(std::move(model), *state_);
    initialize_pool(next->model, next->pool);
    next->compression_factor = block_compression(next->pool);
    next->bounds.scheduled.resize(next->model.chunks.size());
    next->models = std::move(state_->models);
    state_ = std::move(next);
}

Model World::snapshot(std::vector<std::uint16_t>* material_ids) const {
    return snapshot_model(state_->model, state_->actual_size, material_ids);
}

WorldView World::view() const {
    const auto& model = state_->model;
    return {model.size,
            state_->actual_size,
            model.chunks,
            model.blocks,
            model.voxels,
            state_->packed_materials,
            state_->revision,
            state_->chunk_revision,
            state_->geometry_revision,
            state_->material_revision,
            state_->changed_chunks,
            state_->pool.changed_blocks,
            state_->pool.changed_voxels,
            state_->invalidated_chunks,
            state_->gpu_authoritative};
}

Vec3i World::size() const {
    return state_->model.size;
}

Vec3i World::actual_size() const {
    return state_->actual_size;
}

double World::compression_factor() const {
    return state_->compression_factor;
}

std::uint16_t World::at(Vec3i position) const {
    if (!inside(position, size())) {
        return 0;
    }
    const Vec3i chunk{position.x / 16, position.y / 16, position.z / 16};
    const auto found = state_->edits.find(flatten(chunk, chunk_size(size())));
    if (found != state_->edits.end()) {
        return found->second.voxels
                   ? (*found->second.voxels)[flatten(
                         {position.x % 16, position.y % 16, position.z % 16}, {16, 16, 16})]
                   : found->second.material;
    }
    return voxel_at(state_->model, position);
}

void World::set(Vec3i position, std::uint16_t material) {
    if (!inside(position, size())) {
        return;
    }
    if (!has_material(material)) {
        throw std::runtime_error("Selected material is outside palette");
    }
    const Vec3i chunk{position.x / 16, position.y / 16, position.z / 16};
    const auto index = flatten(chunk, chunk_size(size()));
    auto found = state_->edits.find(index);
    if (found == state_->edits.end() || !found->second.voxels) {
        auto dense = std::make_unique<DenseChunk>(chunk_voxels(chunk));
        state_->edits[index].voxels = std::move(dense);
        found = state_->edits.find(index);
    }
    (*found->second
          .voxels)[flatten({position.x % 16, position.y % 16, position.z % 16}, {16, 16, 16})] =
        material;
}

void World::rebuild_bounds() {
    if (state_->gpu_authoritative) {
        state_->invalidated_chunks.clear();
        for (std::size_t index = 0; index < state_->model.chunks.size(); ++index) {
            state_->invalidated_chunks.push_back(static_cast<std::uint32_t>(index));
        }
        ++state_->chunk_revision;
        return;
    }
    if (state_->entities.empty()) {
        chunk_bounds(state_->model.chunks, chunk_size(size()));
    } else {
        std::vector<std::size_t> changed;
        for (std::size_t index = 0; index < state_->entity_gpu.chunks.size(); ++index) {
            if (state_->entity_gpu.chunks[index] != 0) {
                changed.push_back(index);
            }
        }
        invalidate_bounds({state_->model.chunks, state_->entity_gpu.chunks, chunk_size(size())},
                          changed, state_->bounds);
    }
    ++state_->chunk_revision;
}

void World::update_bounds(std::size_t cell_budget) {
    if (state_->gpu_authoritative) {
        return;
    }
    if (advance_bounds({state_->model.chunks, state_->entity_gpu.chunks, chunk_size(size())},
                       state_->bounds, cell_budget)) {
        ++state_->chunk_revision;
    }
}

} // namespace engine::world::model
