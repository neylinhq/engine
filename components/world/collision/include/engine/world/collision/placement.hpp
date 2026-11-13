#ifndef ENGINE_WORLD_COLLISION_PLACEMENT_HPP
#define ENGINE_WORLD_COLLISION_PLACEMENT_HPP

#include <engine/motion/character/collision.hpp>
#include <engine/world/model/world.hpp>

namespace engine::world::collision {

struct CapsulePlacement {
    motion::character::Capsule capsule;
    double voxel_metres;

    // The placement must outlive the guarded edit.
    [[nodiscard]] model::PlacementGuard guard() const&;
    model::PlacementGuard guard() const&& = delete;
};

} // namespace engine::world::collision

#endif
