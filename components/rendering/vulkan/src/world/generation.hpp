#ifndef ENGINE_RENDERING_VULKAN_SRC_WORLD_GENERATION_HPP
#define ENGINE_RENDERING_VULKAN_SRC_WORLD_GENERATION_HPP

#include <array>
#include <cstdint>
#include <engine/world/model/types.hpp>
#include <vector>

namespace engine::rendering::vulkan {

class WorldCompute;
[[nodiscard]] bool uniform_source(const world::model::Model& source, world::model::Vec3i requested,
                                  int segment_groups);

struct GenerationResult {
    world::model::Model model;
    world::model::Vec3i actual_size;
    std::vector<std::array<std::uint32_t, 3>> hash;
    double compression = 1;
    std::uint32_t logical_voxels = 0;
    std::uint32_t block_words = 0;
    std::uint32_t segments = 0;
};

[[nodiscard]] GenerationResult import_uniform(WorldCompute& compute,
                                              const world::model::Model& source);

[[nodiscard]] GenerationResult generate_world(WorldCompute& compute,
                                              const world::model::Model& source,
                                              world::model::Vec3i requested, int segment_groups);

} // namespace engine::rendering::vulkan

#endif
