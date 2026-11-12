#include "helpers.hpp"

#include <algorithm>

namespace engine::world::model {
namespace {

constexpr std::array<unsigned, 6> kMasks{0x3D, 0x3E, 0x37, 0x3B, 0x1F, 0x2F};
constexpr std::array<Vec3i, 6> kDirections{
    {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};

bool empty(ChunkBounds field, std::size_t index) {
    return (field.chunks[index] >> 30) == 0 && field.entities[index] == 0;
}

void enqueue(BoundWork& work, std::size_t index) {
    if (work.scheduled[index] == 0) {
        work.scheduled[index] = 1;
        work.queue.push_back(index);
    }
}

unsigned matching(std::uint32_t neighbor, std::uint32_t current) {
    unsigned result = 0;
    for (unsigned direction = 0; direction < 6; ++direction) {
        if (((neighbor >> (direction * 5)) & 31U) >= ((current >> (direction * 5)) & 31U)) {
            result |= 1U << direction;
        }
    }
    return result;
}

bool relax(ChunkBounds field, std::size_t index, Vec3i position) {
    auto& current = field.chunks[index];
    const auto old = current;
    for (unsigned direction = 0; direction < 6; ++direction) {
        const auto offset = kDirections[direction];
        const Vec3i next{position.x + offset.x, position.y + offset.y, position.z + offset.z};
        if (!inside(next, field.size) || ((current >> (direction * 5)) & 31U) == 31) {
            continue;
        }
        const auto neighbor = flatten(next, field.size);
        if (empty(field, neighbor) &&
            (matching(field.chunks[neighbor], current) & kMasks[direction]) == kMasks[direction]) {
            current += 1U << (direction * 5);
        }
    }
    return old != current;
}

void schedule_neighbors(ChunkBounds field, BoundWork& work, Vec3i position) {
    for (auto offset : kDirections) {
        const Vec3i next{position.x + offset.x, position.y + offset.y, position.z + offset.z};
        if (inside(next, field.size)) {
            const auto index = flatten(next, field.size);
            if (empty(field, index)) {
                enqueue(work, index);
            }
        }
    }
}

struct Region {
    Vec3i lower;
    Vec3i upper;
};

Region affected_region(Vec3i size, std::span<const std::size_t> changed) {
    Region region{size, {0, 0, 0}};
    for (auto index : changed) {
        const auto position = unflatten(index, size);
        region.lower = {std::min(region.lower.x, position.x), std::min(region.lower.y, position.y),
                        std::min(region.lower.z, position.z)};
        region.upper = {std::max(region.upper.x, position.x), std::max(region.upper.y, position.y),
                        std::max(region.upper.z, position.z)};
    }
    region.lower = {std::max(0, region.lower.x - 31), std::max(0, region.lower.y - 31),
                    std::max(0, region.lower.z - 31)};
    region.upper = {std::min(size.x - 1, region.upper.x + 31),
                    std::min(size.y - 1, region.upper.y + 31),
                    std::min(size.z - 1, region.upper.z + 31)};
    return region;
}

} // namespace

void invalidate_bounds(ChunkBounds bounds, std::span<const std::size_t> changed, BoundWork& work) {
    if (changed.empty()) {
        return;
    }
    const auto region = affected_region(bounds.size, changed);
    const Vec3i extent{region.upper.x - region.lower.x + 1, region.upper.y - region.lower.y + 1,
                       region.upper.z - region.lower.z + 1};
    for (std::size_t local = 0; local < volume(extent); ++local) {
        const auto relative = unflatten(local, extent);
        const Vec3i position{region.lower.x + relative.x, region.lower.y + relative.y,
                             region.lower.z + relative.z};
        const auto index = flatten(position, bounds.size);
        if ((bounds.chunks[index] >> 30) == 0) {
            bounds.chunks[index] = 0;
            if (bounds.entities[index] == 0) {
                enqueue(work, index);
            }
        }
    }
}

bool advance_bounds(ChunkBounds bounds, BoundWork& work, std::size_t budget) {
    bool changed = false;
    for (std::size_t processed = 0; processed < budget && !work.queue.empty(); ++processed) {
        const auto index = work.queue.front();
        work.queue.pop_front();
        work.scheduled[index] = 0;
        if (!empty(bounds, index)) {
            continue;
        }
        const auto position = unflatten(index, bounds.size);
        if (relax(bounds, index, position)) {
            changed = true;
            enqueue(work, index);
            schedule_neighbors(bounds, work, position);
        }
    }
    return changed;
}

} // namespace engine::world::model
