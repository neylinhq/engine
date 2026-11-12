#ifndef ENGINE_WORLD_MODEL_SRC_STATE_HPP
#define ENGINE_WORLD_MODEL_SRC_STATE_HPP

#include "helpers.hpp"

#include <engine/world/model/world.hpp>

#include <unordered_map>

namespace engine::world::model {

struct ChunkEdit {
    std::unique_ptr<DenseChunk> voxels;
    std::uint16_t material = 0;
};

struct World::State {
    Model model;
    Vec3i actual_size;
    BlockPool pool;
    std::unordered_map<std::size_t, ChunkEdit> edits;
    std::vector<std::array<std::uint32_t, 4>> packed_materials;
    std::vector<Model> models;
    std::vector<EntityType> entity_types;
    std::vector<EntityInstance> entities;
    std::vector<std::size_t> entity_chunks;
    EntityGpuData entity_gpu;
    std::uint64_t revision = 0;
    std::uint64_t chunk_revision = 0;
    std::uint64_t geometry_revision = 0;
    std::uint64_t material_revision = 0;
    BoundWork bounds;
    double entity_time = 0;
    double compression_factor = 1;
    std::uint64_t material_context = 0;
    std::uint64_t next_anonymous_id = 0;
    bool gpu_authoritative = false;
    std::vector<std::uint32_t> changed_chunks;
    std::vector<std::uint32_t> invalidated_chunks;

    void invalidate(std::span<const std::size_t> changed);
};

} // namespace engine::world::model

#endif
