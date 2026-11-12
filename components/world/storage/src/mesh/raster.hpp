#ifndef ENGINE_WORLD_STORAGE_SRC_MESH_RASTER_HPP
#define ENGINE_WORLD_STORAGE_SRC_MESH_RASTER_HPP

#include "../import.hpp"

namespace engine::world::storage {

[[nodiscard]] std::vector<ColorVoxel> voxelize_mesh(std::span<const Triangle> triangles,
                                                    int resolution);

} // namespace engine::world::storage

#endif
