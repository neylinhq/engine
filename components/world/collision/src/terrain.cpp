#include <engine/world/collision/terrain.hpp>

#include "query.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>

namespace engine::world::collision {
namespace {

namespace character = motion::character;
using foundation::geometry::Box;

struct Cast {
    const CapsuleQuery& query;
    character::SweepFilter filter;
    std::optional<character::SweepHit> hit;
};

bool sweep_solid(void* context, Box solid) {
    auto& cast = *static_cast<Cast*>(context);
    const auto fraction = cast.hit ? cast.hit->fraction : 1.0;
    if (const auto hit = cast_solid(cast.query, solid, fraction);
        hit && (!cast.filter.accept || cast.filter.accept(*hit, cast.filter.context))) {
        cast.hit = hit;
    }
    return cast.hit && cast.hit->fraction == 0;
}

struct Overlap {
    const CapsuleQuery& query;
    std::optional<character::Penetration> contact;
};

bool collect_penetration(void* context, Box solid) {
    auto& overlap = *static_cast<Overlap*>(context);
    if (const auto contact =
            overlap_solid(overlap.query.capsule, solid, overlap.query.voxel_meters)) {
        if (contact->normal == Vec3{} || !overlap.contact ||
            contact->depth > overlap.contact->depth) {
            overlap.contact = contact;
        }
    }
    return overlap.contact && overlap.contact->normal == Vec3{};
}

} // namespace

Terrain::Terrain(model::WorldView view, double voxel_meters)
    : view_(view), voxel_meters_(voxel_meters) {
    if (!std::isfinite(voxel_meters) || voxel_meters <= 0) {
        throw std::invalid_argument("Voxel size must be positive and finite");
    }
}

std::optional<character::SweepHit> Terrain::sweep(character::Capsule capsule, Vec3 displacement,
                                                  character::SweepFilter filter) const {
    if (displacement == Vec3{}) {
        return std::nullopt;
    }
    const auto query = make_query(capsule, displacement, voxel_meters_);
    Cast cast{query, filter, std::nullopt};
    static_cast<void>(view_.visit_solids(query.bounds, sweep_solid, &cast));
    return cast.hit;
}

std::optional<character::Penetration> Terrain::penetration(character::Capsule capsule) const {
    const auto query = make_query(capsule, {}, voxel_meters_);
    Overlap overlap{query, std::nullopt};
    static_cast<void>(view_.visit_solids(query.bounds, collect_penetration, &overlap));
    return overlap.contact;
}

} // namespace engine::world::collision
