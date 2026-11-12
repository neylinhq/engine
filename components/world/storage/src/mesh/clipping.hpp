#ifndef ENGINE_WORLD_STORAGE_SRC_MESH_CLIPPING_HPP
#define ENGINE_WORLD_STORAGE_SRC_MESH_CLIPPING_HPP

#include "../import.hpp"

namespace engine::world::storage {

struct Fragment {
    std::array<model::Vec3, 3> points;
    std::array<TextureCoordinate, 3> coordinates;
};

struct WeightedCoordinate {
    float weight = 0;
    TextureCoordinate coordinate;
};

struct ClipWorkspace {
    std::vector<Fragment> current;
    std::vector<Fragment> next;
};

[[nodiscard]] WeightedCoordinate clip_voxel(const Fragment& fragment, model::Vec3i voxel,
                                            float original_area, ClipWorkspace& workspace);

} // namespace engine::world::storage

#endif
