#ifndef ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_RENDER_HELPERS_HPP
#define ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_RENDER_HELPERS_HPP

#include "../../src/world/compute.hpp"

namespace engine::rendering::vulkan::test {

void verify_render_helpers(WorldCompute& compute);
void verify_first_hit_data(WorldCompute& compute);

} // namespace engine::rendering::vulkan::test

#endif
