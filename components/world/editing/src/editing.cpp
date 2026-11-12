#include <engine/world/editing/editing.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <stdexcept>

namespace engine::world::editing {
namespace {

bool inside(model::Vec3i position, model::Vec3i size) {
    return position.x >= 0 && position.y >= 0 && position.z >= 0 && position.x < size.x &&
           position.y < size.y && position.z < size.z;
}

int lower_bound(double center, double radius, int size) {
    return static_cast<int>(
        std::clamp(std::floor(center - radius), 0.0, static_cast<double>(size)));
}

void stamp_row(model::World& world, const model::Model& source, model::Vec3i local,
               model::Vec3i origin, int end_x) {
    for (; local.x < end_x; ++local.x) {
        const auto material = model::voxel_at(source, local);
        if (material != 0) {
            world.set({origin.x + local.x, origin.y + local.y, origin.z + local.z}, material);
        }
    }
}

} // namespace

void flood_fill(model::World& world, model::Vec3i start, std::uint16_t material) {
    const auto previous = world.at(start);
    if (previous == 0 || previous == material) {
        return;
    }
    const std::array<model::Vec3i, 6> directions{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    std::deque<model::Vec3i> queue{start};
    world.set(start, material);
    while (!queue.empty()) {
        const auto position = queue.front();
        queue.pop_front();
        for (auto direction : directions) {
            const model::Vec3i next{position.x + direction.x, position.y + direction.y,
                                    position.z + direction.z};
            if (inside(next, world.size()) && world.at(next) == previous) {
                world.set(next, material);
                queue.push_back(next);
            }
        }
    }
    world.commit_edits();
}

void stamp_model(model::World& world, const model::Model& source, model::Vec3i origin) {
    const auto world_size = world.size();
    const auto origin_vector =
        model::Vec3{static_cast<double>(origin.x), static_cast<double>(origin.y),
                    static_cast<double>(origin.z)};
    const model::Vec3i lower{lower_bound(-origin_vector.x, 0, source.size.x),
                             lower_bound(-origin_vector.y, 0, source.size.y),
                             lower_bound(-origin_vector.z, 0, source.size.z)};
    const model::Vec3i upper{lower_bound(world_size.x - origin_vector.x, 0, source.size.x),
                             lower_bound(world_size.y - origin_vector.y, 0, source.size.y),
                             lower_bound(world_size.z - origin_vector.z, 0, source.size.z)};
    for (auto z = lower.z; z < upper.z; ++z) {
        for (auto y = lower.y; y < upper.y; ++y) {
            stamp_row(world, source, {lower.x, y, z}, origin, upper.x);
        }
    }
    world.commit_edits();
}

} // namespace engine::world::editing
