#pragma once

#include <engine/foundation/geometry/vector.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::world::model {

using Vec3 = foundation::geometry::Vec3;

struct IVec3 {
    int x = 0;
    int y = 0;
    int z = 0;
    constexpr bool operator==(const IVec3&) const = default;
};

enum class MaterialBase : std::uint32_t { diffuse, emissive, metallic_rough, metallic_mirror };
enum class MaterialLayer : std::uint32_t { none, metallic_rough = 2, metallic_mirror = 3 };

struct Material {
    std::string name;
    Vec3 color_base{};
    Vec3 color_layer{};
    MaterialBase base = MaterialBase::diffuse;
    MaterialLayer layer = MaterialLayer::none;
    float roughness = 0;
};

struct Model {
    std::string name;
    IVec3 size;
    std::vector<Material> materials;
    std::vector<std::uint32_t> chunks;
    std::vector<std::uint32_t> blocks;
    std::vector<std::uint32_t> voxels;
    // Zero means local file palette; nonzero identities are runtime-only.
    std::uint64_t material_context = 0;
};

// The original generator reads this mirror before GPU AADF construction.
// Hash entries retain the original {voxel word offset, use count, hash} layout.
struct GeneratedWorld {
    Model model;
    IVec3 actual_size;
    std::vector<std::array<std::uint32_t, 3>> hash;
    double compression = 1;
    std::uint32_t logical_voxels = 0;
    std::uint32_t block_words = 0;
};

struct Sample {
    IVec3 position;
    std::uint16_t material = 0;
};

struct Hit {
    bool hit = false;
    double distance = 0;
    IVec3 voxel;
    Vec3 normal;
    std::uint16_t material = 0;
    std::uint32_t steps = 0;
};

struct EntityType {
    std::string name;
    IVec3 size;
    std::vector<std::uint32_t> voxels;
    std::uint32_t voxel_start = 0;
};

struct EntityInstance {
    std::uint32_t type = 0;
    Vec3 position;
    std::array<float, 4> quaternion{0, 0, 0, 1};
};

struct EntityGpuData {
    std::vector<std::uint32_t> chunks;
    std::vector<std::array<std::uint32_t, 5>> instances;
    std::vector<std::array<std::uint32_t, 4>> history;
    std::vector<std::uint32_t> voxels;
    std::uint64_t revision = 0;
    std::uint64_t voxel_revision = 0;
    std::vector<std::uint32_t> changed_chunks;
    std::uint32_t entity_count = 0;
};

[[nodiscard]] IVec3 chunk_size(IVec3 size);
[[nodiscard]] std::uint16_t voxel_at(const Model& model, IVec3 position);
[[nodiscard]] Model build_model(IVec3 size, std::vector<Material> materials,
                                const std::vector<Sample>& samples);
void validate(const Model& model);

} // namespace engine::world::model
