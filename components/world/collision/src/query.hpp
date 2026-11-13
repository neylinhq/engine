#ifndef ENGINE_WORLD_COLLISION_SRC_QUERY_HPP
#define ENGINE_WORLD_COLLISION_SRC_QUERY_HPP

#include <engine/foundation/geometry/vector.hpp>
#include <engine/motion/character/collision.hpp>
#include <optional>

namespace engine::world::collision {

using foundation::geometry::Box;
using foundation::geometry::Vec3;

struct CapsuleQuery {
    motion::character::Capsule capsule;
    Vec3 displacement;
    double voxel_meters;
    Box bounds;
};

[[nodiscard]] CapsuleQuery make_query(motion::character::Capsule capsule, Vec3 displacement,
                                      double voxel_meters);
[[nodiscard]] std::optional<motion::character::SweepHit> cast_solid(const CapsuleQuery& query,
                                                                    Box solid, double max_fraction);
[[nodiscard]] std::optional<motion::character::Penetration>
overlap_solid(motion::character::Capsule capsule, Box solid, double voxel_meters);

} // namespace engine::world::collision

#endif
