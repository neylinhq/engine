#ifndef ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_BOUNDS_ORACLE_HPP
#define ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_BOUNDS_ORACLE_HPP

#include "update_probe.hpp"

namespace engine::rendering::vulkan::test {

void verify_local_bounds(const world::model::Model& raw, const world::model::Model& gpu);
void verify_chunk_bounds(world::model::Vec3i size, std::span<const GroupRecord> chunks);
void verify_chunk_occupancy(const world::model::World& world, std::span<const GroupRecord> chunks);
void verify_queue_empty(WorldCompute& compute);

} // namespace engine::rendering::vulkan::test

#endif
