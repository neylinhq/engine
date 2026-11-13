#include "contacts.hpp"

#include <engine/foundation/geometry/vector.hpp>
#include <engine/motion/character/motor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace engine::motion::character {
namespace {

constexpr double kWalkableNormal = 0.71;
constexpr double kPerchRadius = 0.8 * kRadius;
constexpr double kFloorHeightTolerance = 0.0025;
// Height of the walkable part of the capsule's lower hemisphere.
constexpr double kSupportDepth = kRadius * (1 - kWalkableNormal);
constexpr double kRecoveryLimit = kRadius * 0.5;
constexpr double kRecoveryClearance = 0.001;
constexpr double kStepForwardProbe = 0.15;
constexpr int kMaxContacts = 4;

Vec3 remove_inward(Vec3 value, Vec3 normal) {
    return value - normal * std::min(0.0, dot(value, normal));
}

// Clip against the current plane, retaining motion along a crease at a second plane.
Vec3 constrain(Vec3 value, const std::array<Vec3, kMaxContacts>& planes, int count) {
    for (int index = 0; index < count; ++index) {
        if (dot(value, planes[index]) >= -kMovementTolerance) {
            continue;
        }
        value = remove_inward(value, planes[index]);
        for (int previous = 0; previous < index; ++previous) {
            if (dot(value, planes[previous]) < -kMovementTolerance) {
                const auto crease = normalized(cross(planes[index], planes[previous]));
                value = crease * dot(value, crease);
            }
        }
    }
    for (int index = 0; index < count; ++index) {
        if (dot(value, planes[index]) < -kMovementTolerance) {
            return {};
        }
    }
    return value;
}

bool walkable(SweepHit hit, Vec3 feet) {
    const auto offset = hit.point - feet;
    return hit.normal.y >= kWalkableNormal &&
           offset.x * offset.x + offset.z * offset.z <= kPerchRadius * kPerchRadius &&
           offset.y < kRadius;
}

double travel_fraction(const Collision& collision, Vec3 feet, Vec3 delta) {
    if (delta == Vec3{}) {
        return 1;
    }
    const auto hit = collision.sweep(body_at(feet), delta);
    return hit ? hit->fraction : 1;
}

struct Floor {
    double stop;
    double support_height;
    double obstruction_height;
    bool walkable_contact;
};

struct SupportProbe {
    Vec3 start;
    Vec3 down;
};

bool accept_support(const SweepHit& hit, const void* context) {
    const auto& probe = *static_cast<const SupportProbe*>(context);
    return walkable(hit, probe.start + probe.down * hit.fraction);
}

std::optional<Floor> probe_floor(const Collision& collision, Vec3 feet, double distance) {
    const Vec3 down{0, -distance, 0};
    const auto hit = collision.sweep(body_at(feet), down);
    if (!hit) {
        return std::nullopt;
    }
    const auto stop = feet + down * hit->fraction;
    if (walkable(*hit, stop)) {
        return Floor{stop.y, hit->point.y, hit->point.y, true};
    }
    // A riser may be the first blocking contact while a nearby tread still supports the body.
    const SupportProbe probe{feet, {0, down.y * hit->fraction - kSupportDepth, 0}};
    const auto support = collision.sweep(body_at(feet), probe.down, {accept_support, &probe});
    if (!support) {
        return std::nullopt;
    }
    return Floor{stop.y, support->point.y, hit->point.y, false};
}

Vec3 adjust_floor(const Collision& collision, Vec3 feet, const Floor& floor) {
    const auto separation = feet.y - floor.stop;
    const auto change = kFloorClearance - separation;
    if (std::abs(change) <= kFloorHeightTolerance) {
        return feet;
    }
    if (change < 0) {
        return {feet.x, floor.stop + kFloorClearance, feet.z};
    }
    if (!floor.walkable_contact) {
        return feet;
    }
    const Vec3 delta{0, change, 0};
    return feet + delta * travel_fraction(collision, feet, delta);
}

// A short per-tick move may reach the rounded edge before it reaches the step's top.
std::optional<Floor> step_support_ahead(const Collision& collision, Vec3 forward, Vec3 displacement,
                                        double distance) {
    const auto probe_delta = normalized(displacement) * kStepForwardProbe;
    const auto probe = forward + probe_delta * travel_fraction(collision, forward, probe_delta);
    const auto support = probe_floor(collision, probe, distance);
    const Vec3 down{0, -distance, 0};
    const auto contact = collision.sweep(body_at(forward), down);
    if (!support || !contact || contact->normal.y <= 0) {
        return std::nullopt;
    }
    const auto stop = forward.y + down.y * contact->fraction;
    return Floor{std::max(stop, support->stop), support->support_height,
                 std::max(contact->point.y, support->obstruction_height), false};
}

} // namespace

Capsule body_at(Vec3 feet) {
    return {feet, kRadius, kHeight};
}

std::optional<Vec3> recover_position(const Collision& collision, Vec3 feet) {
    double traveled = 0;
    for (int iteration = 0; iteration < kMaxContacts; ++iteration) {
        const auto hit = collision.penetration(body_at(feet));
        if (!hit) {
            return feet;
        }
        const auto distance = hit->depth + kRecoveryClearance;
        traveled += distance;
        if (hit->normal == Vec3{} || traveled > kRecoveryLimit) {
            return std::nullopt;
        }
        feet = feet + hit->normal * distance;
    }
    return collision.penetration(body_at(feet)) ? std::nullopt : std::optional{feet};
}

Travel slide(const Collision& collision, Vec3 feet, Vec3 velocity, double vertical_limit) {
    auto remaining_time = kTick;
    std::array<Vec3, kMaxContacts> planes{};
    bool blocked = false;
    for (int index = 0; index < kMaxContacts; ++index) {
        const auto delta = velocity * remaining_time;
        if (dot(delta, delta) < kMovementTolerance * kMovementTolerance) {
            break;
        }
        const auto hit = collision.sweep(body_at(feet), delta);
        if (!hit) {
            feet = feet + delta;
            break;
        }
        blocked = true;
        feet = feet + delta * hit->fraction;
        remaining_time *= 1 - hit->fraction;
        if (hit->normal == Vec3{}) {
            return {feet, {}, true};
        }
        planes[index] = hit->normal;
        auto clipped = constrain(velocity, planes, index + 1);
        if (clipped.y > vertical_limit) {
            clipped = clipped * (vertical_limit / clipped.y);
            auto remaining = velocity - clipped;
            remaining.y = 0;
            auto horizontal_planes = planes;
            for (int plane = 0; plane <= index; ++plane) {
                horizontal_planes[plane].y = 0;
                horizontal_planes[plane] = normalized(horizontal_planes[plane]);
            }
            clipped = clipped + constrain(remaining, horizontal_planes, index + 1);
        }
        velocity = clipped;
    }
    return {feet, velocity, blocked};
}

std::optional<Vec3> find_floor(const Collision& collision, Vec3 feet, double distance) {
    const auto floor = probe_floor(collision, feet, distance);
    return floor ? std::optional{adjust_floor(collision, feet, *floor)} : std::nullopt;
}

std::optional<Vec3> step_up(const Collision& collision, Vec3 feet, Vec3 displacement, Vec3 flat) {
    const Vec3 up{0, kStepHeight, 0};
    const auto raised = feet + up * travel_fraction(collision, feet, up);
    if (raised.y <= feet.y + kMovementTolerance) {
        return std::nullopt;
    }
    const auto forward = raised + displacement * travel_fraction(collision, raised, displacement);
    const auto distance = raised.y - feet.y + kGroundProbe;
    auto floor = probe_floor(collision, forward, distance);
    if (!floor || floor->stop + kFloorClearance <= feet.y + kFloorHeightTolerance) {
        floor = step_support_ahead(collision, forward, displacement, distance);
    }
    const auto initial = probe_floor(collision, feet, kGroundProbe);
    if (!floor || !initial ||
        std::max(floor->support_height, floor->obstruction_height) - initial->support_height >
            kStepHeight + kMovementTolerance) {
        return std::nullopt;
    }
    const Vec3 placed{forward.x, std::min(forward.y, floor->stop + kFloorClearance), forward.z};
    if (placed.y <= feet.y + kFloorHeightTolerance || placed.y - feet.y > kStepHeight ||
        dot(placed - flat, displacement) <= kMovementTolerance * kMovementTolerance) {
        return std::nullopt;
    }
    return placed;
}

} // namespace engine::motion::character
