#ifndef ENGINE_MOTION_CHARACTER_COLLISION_HPP
#define ENGINE_MOTION_CHARACTER_COLLISION_HPP

#include <engine/foundation/geometry/vector.hpp>
#include <optional>

namespace engine::motion::character {

using foundation::geometry::Vec3;

// Upright capsule. The origin is the lowest point, and all lengths are in meters.
struct Capsule {
    Vec3 feet;
    double radius;
    double height;
};

struct SweepHit {
    double fraction;
    Vec3 normal;
    Vec3 point;
};

struct SweepFilter {
    bool (*accept)(const SweepHit& hit, const void* context) = nullptr;
    const void* context = nullptr;
};

struct Penetration {
    Vec3 normal{};
    double depth = 0;
};

class Collision {
  public:
    virtual ~Collision() = default;
    // First blocking contact. The fraction is safe travel; the point lies on the obstacle.
    // The normal points out of the obstacle. A zero normal means no direction is available.
    // A filter runs synchronously. Rejected contacts do not shorten the query.
    [[nodiscard]] virtual std::optional<SweepHit> sweep(Capsule capsule, Vec3 displacement,
                                                        SweepFilter filter = {}) const = 0;
    // Deepest overlap. An overlap without a reliable correction has a zero normal.
    [[nodiscard]] virtual std::optional<Penetration> penetration(Capsule capsule) const = 0;
};

} // namespace engine::motion::character

#endif
