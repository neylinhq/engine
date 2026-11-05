#pragma once

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

[[nodiscard]] WeightedCoordinate clip_voxel(const Fragment& fragment, model::IVec3 voxel,
                                            float original_area, ClipWorkspace& workspace);

} // namespace engine::world::storage
