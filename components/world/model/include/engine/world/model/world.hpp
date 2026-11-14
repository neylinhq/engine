#ifndef ENGINE_WORLD_MODEL_WORLD_HPP
#define ENGINE_WORLD_MODEL_WORLD_HPP

#include <engine/world/model/types.hpp>

#include <memory>
#include <span>
#include <string_view>

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

struct MaterialPalette {
    // Optional properties in local ID order. Empty uses the model's properties.
    std::span<const Material> values{};
    // Explicit keys register every local entry, including zero. An empty key allocates its own ID.
    // Repeated nonempty keys share the ID and properties of the first entry in the source.
    // With no keys, unique embedded names are used and unused entry zero is skipped.
    std::span<const std::string_view> keys{};
};

class World {
  public:
    explicit World(unsigned worker_count = 1);
    ~World();
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Replace the scene on the CPU using the same material layout as GPU generation.
    void replace(Model model, Vec3i requested_size = {}, int segment_groups = 4);
    // Prepare a scene without changing the active world. An override uses local palette order.
    // For a local source, material_ids receives the runtime ID of each local entry.
    // Adopt the result into this world before changing its materials.
    [[nodiscard]] Model
    prepare_generation(Model source, MaterialPalette palette = {},
                       std::vector<std::uint16_t>* material_ids = nullptr) const;
    void adopt_generated(GeneratedWorld generated);
    void acknowledge_changes();
    // If requested, return the runtime ID for each compacted palette slot. Slot zero is reserved.
    [[nodiscard]] Model snapshot(std::vector<std::uint16_t>* material_ids = nullptr) const;
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
    [[nodiscard]] bool has_material(std::uint16_t id) const;
    [[nodiscard]] std::uint16_t add_material(Material material);
    // Edit properties. Changing the name also changes the binding of this runtime ID.
    void set_material(std::uint16_t index, Material material);
    // Apply updates to the listed material IDs in the active scene.
    // IDs must be distinct and nonzero. Updates keep the source binding keys.
    // Returns whether the effective palette changed. An invalid batch leaves the scene intact.
    [[nodiscard]] bool set_scene_materials(std::span<const MaterialUpdate> updates);
    // Read the active properties and display names in file ID order.
    [[nodiscard]] std::vector<Material>
    scene_palette(std::span<const std::uint16_t> material_ids) const;
    [[nodiscard]] std::span<const Model> models() const;
    // Register a model with its local materials and keep its assigned IDs across scene changes.
    [[nodiscard]] std::size_t add_model(Model model, MaterialPalette palette = {});
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
