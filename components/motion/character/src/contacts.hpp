#ifndef ENGINE_MOTION_CHARACTER_SRC_CONTACTS_HPP
#define ENGINE_MOTION_CHARACTER_SRC_CONTACTS_HPP

#include <engine/motion/character/collision.hpp>
#include <optional>

namespace engine::motion::character {

inline constexpr double kGroundProbe = 0.03;
inline constexpr double kFloorClearance = 0.02;
inline constexpr double kMovementTolerance = 1e-6;

struct Travel {
    Vec3 feet;
    Vec3 velocity;
    bool blocked;
};

[[nodiscard]] Capsule body_at(Vec3 feet);
[[nodiscard]] std::optional<Vec3> recover_position(const Collision& collision, Vec3 feet);
[[nodiscard]] Travel slide(const Collision& collision, Vec3 feet, Vec3 velocity,
                           double vertical_limit);
[[nodiscard]] std::optional<Vec3> find_floor(const Collision& collision, Vec3 feet,
                                             double distance);
[[nodiscard]] std::optional<Vec3> step_up(const Collision& collision, Vec3 feet, Vec3 displacement,
                                          Vec3 flat);

} // namespace engine::motion::character

#endif
