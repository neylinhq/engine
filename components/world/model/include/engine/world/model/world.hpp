#ifndef ENGINE_WORLD_MODEL_WORLD_HPP
#define ENGINE_WORLD_MODEL_WORLD_HPP

#include <engine/world/model/types.hpp>

#include <memory>
#include <span>

namespace engine::world::model {

struct WorldView {
    using SolidVisitor = bool (*)(void* context, foundation::geometry::Box bounds);

    Vec3i size;
    Vec3i actual_size;
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

    // Visit occupied regions that intersect finite, nonempty bounds in voxel coordinates.
    // The callback receives entire regions synchronously. Its true return ends the query.
    [[nodiscard]] bool visit_solids(foundation::geometry::Box bounds, SolidVisitor visitor,
                                    void* context) const;
};

// Rejects a pending batch if a newly occupied voxel within the bounds is blocked.
struct PlacementGuard {
    foundation::geometry::Box bounds{};
    bool (*blocks)(foundation::geometry::Box voxel, const void* context) noexcept = nullptr;
    const void* context = nullptr;
};

class World {
  public:
    explicit World(unsigned worker_count = 1);
    ~World();
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void replace(Model model, Vec3i requested_size = {}, int segment_groups = 4);
    [[nodiscard]] Model prepare_generation(Model source);
    void adopt_generated(GeneratedWorld generated);
    void acknowledge_changes();
    [[nodiscard]] Model snapshot() const;
    [[nodiscard]] WorldView view() const;
    [[nodiscard]] Vec3i size() const;
    [[nodiscard]] Vec3i actual_size() const;
    [[nodiscard]] double compression_factor() const;
    [[nodiscard]] unsigned worker_count() const noexcept {
        return worker_count_;
    }
    [[nodiscard]] std::uint16_t at(Vec3i position) const;
    [[nodiscard]] Hit trace(Vec3 origin, Vec3 direction, double max_distance = 100000) const;
    void set(Vec3i position, std::uint16_t material);
    // Chunk coordinates; includes pending edits. All 4096 cells use X-major order.
    [[nodiscard]] std::array<std::uint16_t, 4096> chunk_voxels(Vec3i chunk) const;
    void set_chunk_voxels(Vec3i chunk, std::span<const std::uint16_t, 4096> voxels);
    void adopt_chunk_voxels(Vec3i chunk, std::unique_ptr<std::array<std::uint16_t, 4096>> voxels);
    void fill_chunk(Vec3i chunk, std::uint16_t material);
    void commit_edits();
    // Bounds use voxel coordinates. The callback runs synchronously and must not edit the world.
    // Invalid bounds or a blocked voxel discard the pending batch without committing it.
    [[nodiscard]] bool commit_edits(PlacementGuard guard);
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
    [[nodiscard]] bool blocks_placement(PlacementGuard guard) const;
    std::unique_ptr<State> state_;
    unsigned worker_count_;
};

} // namespace engine::world::model

#endif
