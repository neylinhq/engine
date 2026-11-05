#pragma once

#include "../../src/world/compute.hpp"

namespace engine::rendering::vulkan::test {

void verify_render_helpers(WorldCompute& compute);
void verify_first_hit_data(WorldCompute& compute);

} // namespace engine::rendering::vulkan::test
