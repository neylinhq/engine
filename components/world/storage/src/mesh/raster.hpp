#pragma once

#include "../import.hpp"

namespace engine::world::storage {

[[nodiscard]] std::vector<ColorVoxel> voxelize_mesh(std::span<const Triangle> triangles,
                                                    int resolution);

} // namespace engine::world::storage
