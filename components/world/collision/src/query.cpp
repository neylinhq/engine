#include "query.hpp"

#include <physics/collision/geometry/collision.hpp>
#include <physics/collision/geometry/hull.hpp>
#include <physics/foundation/math/types.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace engine::world::collision {
namespace {

namespace sdk = physics::collision::geometry;
namespace sdk_math = physics::foundation::math;
namespace character = motion::character;

// The expanded cast compensates for the SDK's shape-cast target and tolerance.
constexpr double kCastPadding = 1.5 * static_cast<double>(sdk::kLinearSlop);
constexpr double kPenetrationTolerance = sdk::kOverlapSlop;
constexpr double kTangencyTolerance = 1e-5;

sdk::Vec3 to_sdk(Vec3 value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

Vec3 from_sdk(sdk::Vec3 value) {
    return {value.x, value.y, value.z};
}

Box swept_bounds(character::Capsule capsule, Vec3 delta) {
    auto lower = capsule.feet - Vec3{capsule.radius, 0, capsule.radius};
    auto upper = capsule.feet + Vec3{capsule.radius, capsule.height, capsule.radius};
    for (unsigned axis = 0; axis < 3; ++axis) {
        lower[axis] += std::min(0.0, delta[axis]);
        upper[axis] += std::max(0.0, delta[axis]);
    }
    return {lower, upper};
}

Vec3 centre_of(Box solid, double voxel_metres) {
    return (solid.lower + solid.upper) * (0.5 * voxel_metres);
}

sdk::Capsule local_capsule(character::Capsule capsule, Vec3 origin) {
    const auto feet = capsule.feet - origin;
    return {to_sdk(feet + Vec3{0, capsule.radius, 0}),
            to_sdk(feet + Vec3{0, capsule.height - capsule.radius, 0}),
            static_cast<float>(capsule.radius)};
}

// A zero-radius convex proxy for an axis-aligned voxel region.
std::array<sdk::Vec3, 8> box_points(Box solid, double voxel_metres) {
    const auto h = to_sdk((solid.upper - solid.lower) * (0.5 * voxel_metres));
    return {{{h.x, h.y, h.z},
             {-h.x, h.y, h.z},
             {-h.x, -h.y, h.z},
             {h.x, -h.y, h.z},
             {h.x, h.y, -h.z},
             {-h.x, h.y, -h.z},
             {-h.x, -h.y, -h.z},
             {h.x, -h.y, -h.z}}};
}

sdk::DistanceOutput capsule_distance(sdk::ShapeProxy box, sdk::ShapeProxy spine) {
    const sdk::DistanceInput input{box, spine, sdk_math::kTransformIdentity, false};
    sdk::SimplexCache cache{};
    return sdk::shape_distance(&input, &cache, nullptr, 0);
}

character::Penetration manifold_penetration(Box solid, double voxel_metres,
                                            const sdk::Capsule& capsule) {
    const auto h = to_sdk((solid.upper - solid.lower) * (0.5 * voxel_metres));
    const auto hull = sdk::make_box_hull(h.x, h.y, h.z);
    std::array<sdk::LocalManifoldPoint, sdk::kMaxManifoldPoints> points{};
    sdk::LocalManifold manifold{.normal = {}, .points = points.data(), .point_count = 0};
    sdk::SimplexCache cache{};
    if (capsule.center1.y == capsule.center2.y) {
        const sdk::Sphere sphere{capsule.center1, capsule.radius};
        sdk::collide_hull_and_sphere(&manifold, static_cast<int>(points.size()), &hull.base,
                                     &sphere, sdk_math::kTransformIdentity, &cache);
    } else {
        sdk::collide_hull_and_capsule(&manifold, static_cast<int>(points.size()), &hull.base,
                                      &capsule, sdk_math::kTransformIdentity, &cache);
    }
    double depth = 0;
    for (int index = 0; index < manifold.point_count; ++index) {
        depth = std::max(depth, -static_cast<double>(points[index].separation));
    }
    if (depth <= kPenetrationTolerance) {
        return {};
    }
    return {from_sdk(manifold.normal), depth};
}

} // namespace

CapsuleQuery make_query(character::Capsule capsule, Vec3 displacement, double voxel_metres) {
    if (!finite(capsule.feet) || !finite(displacement) || !std::isfinite(capsule.radius) ||
        !std::isfinite(capsule.height) || capsule.radius <= 0 ||
        capsule.height < 2 * capsule.radius) {
        throw std::invalid_argument("Invalid capsule query");
    }
    const auto bounds = swept_bounds(capsule, displacement);
    return {capsule,
            displacement,
            voxel_metres,
            {bounds.lower / voxel_metres, bounds.upper / voxel_metres}};
}

std::optional<character::SweepHit> cast_solid(const CapsuleQuery& query, Box solid,
                                              double max_fraction) {
    const auto origin = centre_of(solid, query.voxel_metres);
    const auto vertices = box_points(solid, query.voxel_metres);
    const sdk::ShapeProxy box{vertices.data(), static_cast<int>(vertices.size()), 0};
    const auto capsule = local_capsule(query.capsule, origin);
    const std::array centres{capsule.center1, capsule.center2};
    const sdk::ShapeProxy spine{centres.data(), static_cast<int>(centres.size()), 0};
    const sdk::ShapeProxy moving{spine.points, spine.count,
                                 capsule.radius + static_cast<float>(kCastPadding)};
    const sdk::ShapeCastPairInput input{box,
                                        moving,
                                        sdk_math::kTransformIdentity,
                                        to_sdk(query.displacement),
                                        static_cast<float>(max_fraction),
                                        false};
    const auto hit = sdk::shape_cast(&input);
    if (!hit.hit) {
        return std::nullopt;
    }
    if (hit.fraction == 0) {
        const auto nearest = capsule_distance(box, spine);
        const auto normal = from_sdk(nearest.normal);
        if (nearest.distance >= capsule.radius - kPenetrationTolerance && normal != Vec3{} &&
            dot(query.displacement, normal) >= -kTangencyTolerance * length(query.displacement)) {
            return std::nullopt;
        }
        return character::SweepHit{0, normal, from_sdk(nearest.point_a) + origin};
    }
    return character::SweepHit{std::clamp(static_cast<double>(hit.fraction), 0.0, max_fraction),
                               from_sdk(hit.normal), from_sdk(hit.point) + origin};
}

std::optional<character::Penetration> overlap_solid(character::Capsule body, Box solid,
                                                    double voxel_metres) {
    const auto vertices = box_points(solid, voxel_metres);
    const sdk::ShapeProxy box{vertices.data(), static_cast<int>(vertices.size()), 0};
    const auto capsule = local_capsule(body, centre_of(solid, voxel_metres));
    const std::array centres{capsule.center1, capsule.center2};
    const sdk::ShapeProxy spine{centres.data(), static_cast<int>(centres.size()), 0};
    const auto nearest = capsule_distance(box, spine);
    const auto depth = static_cast<double>(capsule.radius) - nearest.distance;
    if (depth <= kPenetrationTolerance) {
        return std::nullopt;
    }
    const auto normal = from_sdk(nearest.normal);
    if (nearest.distance > 0 && normal != Vec3{}) {
        return character::Penetration{normal, depth};
    }
    return manifold_penetration(solid, voxel_metres, capsule);
}

} // namespace engine::world::collision
