#ifndef ENGINE_WORLD_COLLISION_TERRAIN_HPP
#define ENGINE_WORLD_COLLISION_TERRAIN_HPP

#include <engine/foundation/geometry/vector.hpp>
#include <engine/motion/character/collision.hpp>
#include <engine/world/model/world.hpp>
#include <optional>

namespace engine::world::collision {

// Holds a view of committed data for a single character update. World mutation invalidates it.
class Terrain final : public motion::character::Collision {
  public:
    Terrain(model::WorldView view, double voxel_metres);
    [[nodiscard]] std::optional<motion::character::SweepHit>
    sweep(motion::character::Capsule capsule, foundation::geometry::Vec3 displacement,
          motion::character::SweepFilter filter = {}) const override;
    [[nodiscard]] std::optional<motion::character::Penetration>
    penetration(motion::character::Capsule capsule) const override;

  private:
    model::WorldView view_;
    double voxel_metres_;
};

} // namespace engine::world::collision

#endif
