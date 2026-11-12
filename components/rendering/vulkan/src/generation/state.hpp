#ifndef ENGINE_RENDERING_VULKAN_SRC_GENERATION_STATE_HPP
#define ENGINE_RENDERING_VULKAN_SRC_GENERATION_STATE_HPP

#include "../world/compute.hpp"
#include "../world/generation.hpp"

namespace engine::rendering::vulkan {

struct GenerationState {
    WorldCompute& compute;
    const world::model::Model& source;
    world::model::Vec3i requested;
    world::model::Vec3i dimensions{};
    world::model::Vec3i chunks{};
    world::model::Vec3i segment_counts{};
    std::uint32_t segment_chunks = 0;
    std::uint32_t raw_voxel_words = 0;
    std::uint32_t segment_block_words = 0;
    std::uint32_t map_slots = 0;
    std::uint32_t segments = 0;
    std::array<std::uint32_t, 2> counts{64, 64};
    std::array<std::uint32_t, 65> coefficients{};
};

[[nodiscard]] GenerationState generation_state(WorldCompute& compute,
                                               const world::model::Model& source,
                                               world::model::Vec3i requested, int segment_groups);
void generation_allocate(GenerationState& state);
void generation_segments(GenerationState& state);
[[nodiscard]] GenerationResult generation_readback(GenerationState& state);
void generation_bounds(GenerationState& state);

} // namespace engine::rendering::vulkan

#endif
