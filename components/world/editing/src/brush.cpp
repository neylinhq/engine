#include <engine/world/editing/editing.hpp>

#include <algorithm>
#include <cmath>
#include <engine/foundation/parallel/parallel.hpp>
#include <stdexcept>

namespace engine::world::editing {
namespace {

bool covered(model::IVec3 position, const Brush& brush) {
    const auto x = position.x + 0.5 - brush.center.x;
    const auto y = position.y + 0.5 - brush.center.y;
    const auto z = position.z + 0.5 - brush.center.z;
    if (brush.tool == Tool::cube) {
        return std::max({std::abs(x), std::abs(y), std::abs(z)}) < brush.radius;
    }
    return x * x + y * y + z * z < brush.radius * brush.radius;
}

double row_radius(const Brush& brush, model::IVec3 start) {
    const auto y = start.y + 0.5 - brush.center.y;
    const auto z = start.z + 0.5 - brush.center.z;
    if (brush.tool == Tool::cube) {
        return std::max(std::abs(y), std::abs(z)) < brush.radius ? brush.radius : -1;
    }
    const auto remaining = brush.radius * brush.radius - y * y - z * z;
    return std::sqrt(std::max(0.0, remaining));
}

std::span<std::uint16_t> covered_row(std::span<std::uint16_t, 16> row, const Brush& brush,
                                     model::IVec3 start) {
    const auto radius = row_radius(brush, start);
    if (radius < 0) {
        return {};
    }
    const auto center = brush.center.x - 0.5 - start.x;
    auto first = static_cast<int>(std::clamp(std::floor(center - radius), 0.0, 16.0));
    auto end = static_cast<int>(std::clamp(std::ceil(center + radius) + 1, 0.0, 16.0));
    // Round outwards, then apply the original strict predicate at both endpoints.
    while (first < end && !covered({start.x + first, start.y, start.z}, brush)) {
        ++first;
    }
    while (first < end && !covered({start.x + end - 1, start.y, start.z}, brush)) {
        --end;
    }
    return row.subspan(first, end - first);
}

bool brush_row(std::span<std::uint16_t, 16> row, const Brush& brush, model::IVec3 start) {
    const auto cells = covered_row(row, brush, start);
    const auto material = brush.erase ? std::uint16_t{0} : brush.material;
    if (brush.tool != Tool::paint) {
        const auto changed =
            std::ranges::any_of(cells, [material](auto value) { return value != material; });
        std::ranges::fill(cells, material);
        return changed;
    }
    bool changed = false;
    for (auto& value : cells) {
        if (value == 0) {
            continue;
        }
        changed = changed || value != material;
        value = material;
    }
    return changed;
}

double chunk_distance(model::IVec3 origin, const Brush& brush, bool farthest) {
    double distance = 0;
    const std::array centers{brush.center.x, brush.center.y, brush.center.z};
    const std::array starts{origin.x, origin.y, origin.z};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto low = starts[axis] + 0.5;
        const auto high = starts[axis] + 15.5;
        const auto delta =
            farthest ? std::max(std::abs(low - centers[axis]), std::abs(high - centers[axis]))
                     : std::abs(centers[axis] - std::clamp(centers[axis], low, high));
        distance = brush.tool == Tool::cube ? std::max(distance, delta) : distance + delta * delta;
    }
    return distance;
}

struct ChunkResult {
    model::IVec3 position;
    bool uniform = false;
    std::unique_ptr<std::array<std::uint16_t, 4096>> voxels;
};

void prepare_chunk(const model::World& world, const Brush& brush, ChunkResult& result) {
    const auto chunk = result.position;
    const model::IVec3 origin{chunk.x * 16, chunk.y * 16, chunk.z * 16};
    const auto limit = brush.tool == Tool::cube ? brush.radius : brush.radius * brush.radius;
    if (chunk_distance(origin, brush, false) >= limit) {
        return;
    }
    if (brush.tool != Tool::paint && chunk_distance(origin, brush, true) < limit) {
        result.uniform = true;
        return;
    }
    auto voxels = world.chunk_voxels(chunk);
    bool changed = false;
    for (int row = 0; row < 256; ++row) {
        const model::IVec3 start{origin.x, origin.y + row % 16, origin.z + row / 16};
        changed =
            brush_row(std::span<std::uint16_t, 16>(voxels.data() + row * 16, 16), brush, start) ||
            changed;
    }
    if (changed) {
        result.voxels = std::make_unique<std::array<std::uint16_t, 4096>>(std::move(voxels));
    }
}

void apply_chunks(model::World& world, const Brush& brush, std::span<ChunkResult> chunks) {
    const auto material = brush.erase ? std::uint16_t{0} : brush.material;
    for (auto& chunk : chunks) {
        if (chunk.uniform) {
            world.fill_chunk(chunk.position, material);
        } else if (chunk.voxels) {
            world.adopt_chunk_voxels(chunk.position, std::move(chunk.voxels));
        }
    }
}

int lower_bound(double center, double radius, int size) {
    return static_cast<int>(
        std::clamp(std::floor(center - radius), 0.0, static_cast<double>(size)));
}

int upper_bound(double center, double radius, int size) {
    return static_cast<int>(
        std::clamp(std::floor(center + radius), -1.0, static_cast<double>(size - 1)));
}

} // namespace

void apply_brush(model::World& world, const Brush& brush) {
    if (!foundation::geometry::finite(brush.center) || !std::isfinite(brush.radius) ||
        brush.radius <= 0 || brush.radius > kMaximumBrushRadius) {
        throw std::runtime_error("Brush radius must be finite and in (0,400]");
    }
    if (brush.tool == Tool::none) {
        return;
    }
    const auto size = world.size();
    const model::IVec3 lower{lower_bound(brush.center.x, brush.radius, size.x),
                             lower_bound(brush.center.y, brush.radius, size.y),
                             lower_bound(brush.center.z, brush.radius, size.z)};
    const model::IVec3 upper{upper_bound(brush.center.x, brush.radius, size.x),
                             upper_bound(brush.center.y, brush.radius, size.y),
                             upper_bound(brush.center.z, brush.radius, size.z)};
    if (lower.x > upper.x || lower.y > upper.y || lower.z > upper.z) {
        return;
    }
    std::vector<ChunkResult> chunks;
    for (auto z = lower.z / 16; z <= upper.z / 16; ++z) {
        for (auto y = lower.y / 16; y <= upper.y / 16; ++y) {
            for (auto x = lower.x / 16; x <= upper.x / 16; ++x) {
                chunks.push_back({{x, y, z}, false, nullptr});
            }
        }
    }
    foundation::parallel::for_each_index(
        chunks.size(), [&](std::size_t index) { prepare_chunk(world, brush, chunks[index]); });
    apply_chunks(world, brush, chunks);
    world.commit_edits();
}

} // namespace engine::world::editing
