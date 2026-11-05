#pragma once

#include <engine/world/model/types.hpp>

#include <memory>
#include <span>

namespace engine::world::model {

struct WorldView {
    IVec3 size;
    IVec3 actual_size;
    std::span<const std::uint32_t> chunks;
    std::span<const std::uint32_t> blocks;
    std::span<const std::uint32_t> voxels;
    std::span<const std::array<std::uint32_t, 4>> materials;
    std::uint64_t revision = 0;
    std::uint64_t chunk_revision = 0;
    std::uint64_t geometry_revision = 0;
    std::uint64_t material_revision = 0;
    std::span<const std::uint32_t> changed_chunks;
    std::span<const std::uint32_t> changed_blocks;
    std::span<const std::uint32_t> changed_voxels;
    std::span<const std::uint32_t> invalidated_chunks;
    bool gpu_authoritative = false;
};

class World {
  public:
    World();
    ~World();
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void replace(Model model, IVec3 requested_size = {}, int segment_groups = 4);
    [[nodiscard]] Model prepare_generation(Model source);
    void adopt_generated(GeneratedWorld generated);
    void acknowledge_changes();
    [[nodiscard]] Model snapshot() const;
    [[nodiscard]] WorldView view() const;
    [[nodiscard]] IVec3 size() const;
    [[nodiscard]] IVec3 actual_size() const;
    [[nodiscard]] double compression_factor() const;
    [[nodiscard]] std::uint16_t at(IVec3 position) const;
    [[nodiscard]] Hit trace(Vec3 origin, Vec3 direction, double max_distance = 100000) const;
    void set(IVec3 position, std::uint16_t material);
    // Chunk coordinates; includes pending edits. All 4096 cells use X-major order.
    [[nodiscard]] std::array<std::uint16_t, 4096> chunk_voxels(IVec3 chunk) const;
    void set_chunk_voxels(IVec3 chunk, std::span<const std::uint16_t, 4096> voxels);
    void adopt_chunk_voxels(IVec3 chunk, std::unique_ptr<std::array<std::uint16_t, 4096>> voxels);
    void fill_chunk(IVec3 chunk, std::uint16_t material);
    void commit_edits();
    void rebuild_bounds();
    void update_bounds(std::size_t cell_budget = 32768);
    [[nodiscard]] std::span<const Material> materials() const;
    [[nodiscard]] std::uint16_t add_material(Material material);
    void set_material(std::uint16_t index, Material material);
    [[nodiscard]] std::span<const Model> models() const;
    [[nodiscard]] std::size_t add_model(Model model);
    [[nodiscard]] std::span<const EntityType> entity_types() const;
    [[nodiscard]] std::span<const EntityInstance> entities() const;
    [[nodiscard]] std::size_t create_entity_type(std::size_t model_index);
    [[nodiscard]] std::size_t spawn_entity(std::size_t type_index, Vec3 position);
    void set_entity_position(std::size_t index, Vec3 position);
    void remove_entities_from(std::size_t index);
    void clear_entities();
    void update_entities(double seconds, double rotation_speed, unsigned history_index);
    [[nodiscard]] const EntityGpuData& entity_gpu() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace engine::world::model
