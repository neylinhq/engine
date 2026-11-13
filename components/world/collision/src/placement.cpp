#include <engine/world/collision/placement.hpp>

#include "query.hpp"

#include <engine/foundation/geometry/vector.hpp>

#include <cmath>
#include <stdexcept>

namespace engine::world::collision {
namespace {

bool blocks(foundation::geometry::Box voxel, const void* context) noexcept {
    const auto& placement = *static_cast<const CapsulePlacement*>(context);
    return overlap_solid(placement.capsule, voxel, placement.voxel_metres).has_value();
}

} // namespace

model::PlacementGuard CapsulePlacement::guard() const& {
    if (!std::isfinite(voxel_metres) || voxel_metres <= 0) {
        throw std::invalid_argument("Voxel size must be positive and finite");
    }
    return {make_query(capsule, {}, voxel_metres).bounds, blocks, this};
}

} // namespace engine::world::collision
