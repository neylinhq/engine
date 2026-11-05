#include "raster.hpp"

#include "clipping.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace engine::world::storage {
namespace {

using Vec3 = model::Vec3;
using CoordinateMap = std::unordered_map<std::uint64_t, WeightedCoordinate>;

struct WeightedColor {
    float weight = 0;
    std::uint32_t color = 0;
};

using ColorMap = std::unordered_map<std::uint64_t, WeightedColor>;

struct Raster {
    const Fragment& fragment;
    Vec3 normal;
    std::array<unsigned, 3> axes;
    int resolution = 0;
    float original_area = 0;
};

foundation::geometry::Box bounds(const Fragment& fragment) {
    auto lower = fragment.points[0];
    auto upper = lower;
    for (auto point : fragment.points) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            lower[axis] = std::min(lower[axis], point[axis]);
            upper[axis] = std::max(upper[axis], point[axis]);
        }
    }
    return {lower, upper};
}

std::uint64_t key(model::IVec3 position) {
    return static_cast<std::uint64_t>(position.x) | (static_cast<std::uint64_t>(position.y) << 21) |
           (static_cast<std::uint64_t>(position.z) << 42);
}

void merge_coordinate(CoordinateMap& voxels, model::IVec3 position, WeightedCoordinate value) {
    if (value.weight == 0) {
        return;
    }
    auto [iterator, inserted] = voxels.try_emplace(key(position), value);
    if (!inserted) {
        auto& previous = iterator->second;
        const auto sum = previous.weight + value.weight;
        previous.coordinate.u =
            (previous.weight * previous.coordinate.u + value.weight * value.coordinate.u) / sum;
        previous.coordinate.v =
            (previous.weight * previous.coordinate.v + value.weight * value.coordinate.v) / sum;
        previous.weight = sum;
    }
}

void raster_column(CoordinateMap& voxels, const Raster& raster, model::IVec3 column,
                   ClipWorkspace& workspace) {
    Vec3 center{column.x + 0.5, column.y + 0.5, column.z + 0.5};
    const auto axis = raster.axes[2];
    center[axis] = 0;
    const auto plane = foundation::geometry::dot(raster.normal, raster.fragment.points[0]);
    const auto height =
        (plane - foundation::geometry::dot(raster.normal, center)) / raster.normal[axis];
    const auto radius =
        0.5 + (std::abs(raster.normal[raster.axes[0]]) + std::abs(raster.normal[raster.axes[1]])) /
                  std::abs(raster.normal[axis]) * 0.5;
    const auto lower = std::max(0, static_cast<int>(std::floor(height - radius)));
    const auto upper =
        std::min(raster.resolution - 1, static_cast<int>(std::floor(height + radius)));
    for (auto coordinate = lower; coordinate <= upper; ++coordinate) {
        center[axis] = coordinate;
        const model::IVec3 position{static_cast<int>(center.x), static_cast<int>(center.y),
                                    static_cast<int>(center.z)};
        merge_coordinate(voxels, position,
                         clip_voxel(raster.fragment, position, raster.original_area, workspace));
    }
}

void raster_fragment(CoordinateMap& voxels, const Raster& raster, ClipWorkspace& workspace) {
    const auto box = bounds(raster.fragment);
    const auto first_min = std::max(0, static_cast<int>(std::floor(box.lower[raster.axes[0]])));
    const auto first_max =
        std::min(raster.resolution - 1, static_cast<int>(std::floor(box.upper[raster.axes[0]])));
    const auto second_min = std::max(0, static_cast<int>(std::floor(box.lower[raster.axes[1]])));
    const auto second_max =
        std::min(raster.resolution - 1, static_cast<int>(std::floor(box.upper[raster.axes[1]])));
    for (auto second = second_min; second <= second_max; ++second) {
        for (auto first = first_min; first <= first_max; ++first) {
            std::array<int, 3> position{};
            position[raster.axes[0]] = first;
            position[raster.axes[1]] = second;
            raster_column(voxels, raster, {position[0], position[1], position[2]}, workspace);
        }
    }
}

Fragment part(const Fragment& original, std::array<unsigned, 3> indices,
              const Fragment& midpoints) {
    Fragment result;
    for (unsigned index = 0; index < 3; ++index) {
        const auto reference = indices[index];
        result.points[index] =
            reference < 3 ? original.points[reference] : midpoints.points[reference - 3];
        result.coordinates[index] =
            reference < 3 ? original.coordinates[reference] : midpoints.coordinates[reference - 3];
    }
    return result;
}

std::array<Fragment, 4> subdivide(const Fragment& fragment) {
    Fragment midpoint;
    for (unsigned index = 0; index < 3; ++index) {
        const auto next = (index + 1) % 3;
        midpoint.points[index] = (fragment.points[index] + fragment.points[next]) * 0.5;
        midpoint.coordinates[index] = {
            (fragment.coordinates[index].u + fragment.coordinates[next].u) * 0.5,
            (fragment.coordinates[index].v + fragment.coordinates[next].v) * 0.5};
    }
    return {midpoint, part(fragment, {0, 3, 5}, midpoint), part(fragment, {1, 4, 3}, midpoint),
            part(fragment, {2, 5, 4}, midpoint)};
}

bool small_fragment(const Fragment& fragment) {
    const auto box = bounds(fragment);
    const auto x = std::floor(box.upper.x) - std::floor(box.lower.x) + 1;
    const auto y = std::floor(box.upper.y) - std::floor(box.lower.y) + 1;
    const auto z = std::floor(box.upper.z) - std::floor(box.lower.z) + 1;
    return x * y * z < 512;
}

void raster_triangle(CoordinateMap& voxels, const Triangle& triangle, int resolution,
                     ClipWorkspace& workspace) {
    const auto normal = foundation::geometry::cross(triangle.points[1] - triangle.points[0],
                                                    triangle.points[2] - triangle.points[0]);
    const auto length = foundation::geometry::length(normal);
    if (length < 1e-15) {
        return;
    }
    const auto diagonal =
        (std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z)) / length / std::sqrt(3.0);
    const auto aligned = (diagonal - 1 / std::sqrt(3.0)) / (1 - 1 / std::sqrt(3.0)) < 0.5;
    unsigned dominant = 0;
    for (unsigned axis = 1; axis < 3; ++axis) {
        if (std::abs(normal[axis]) > std::abs(normal[dominant])) {
            dominant = axis;
        }
    }
    const std::array<unsigned, 3> axes{(dominant + 1) % 3, (dominant + 2) % 3, dominant};
    std::vector<Fragment> pending{{triangle.points, triangle.coordinates}};
    while (!pending.empty()) {
        const auto fragment = pending.back();
        pending.pop_back();
        if (aligned || small_fragment(fragment)) {
            raster_fragment(voxels,
                            {fragment, normal, axes, resolution, static_cast<float>(length / 2)},
                            workspace);
        } else {
            const auto pieces = subdivide(fragment);
            pending.insert(pending.end(), pieces.begin(), pieces.end());
        }
    }
}

void merge_colors(ColorMap& output, const CoordinateMap& input, const Triangle& triangle) {
    for (const auto& [index, value] : input) {
        const auto color =
            triangle.texture ? sample_image(*triangle.texture, value.coordinate) : triangle.color;
        auto [iterator, inserted] = output.try_emplace(index, WeightedColor{value.weight, color});
        if (!inserted && iterator->second.weight < value.weight) {
            iterator->second = {value.weight, color};
        }
    }
}

} // namespace

std::vector<ColorVoxel> voxelize_mesh(std::span<const Triangle> triangles, int resolution) {
    ColorMap colors;
    ClipWorkspace workspace;
    workspace.current.reserve(64);
    workspace.next.reserve(64);
    CoordinateMap coordinates;
    for (const auto& triangle : triangles) {
        coordinates.clear();
        raster_triangle(coordinates, triangle, resolution, workspace);
        merge_colors(colors, coordinates, triangle);
    }
    std::vector<ColorVoxel> result;
    result.reserve(colors.size());
    for (const auto& [index, color] : colors) {
        result.push_back(
            {{static_cast<int>(index & 0x1FFFFFU), static_cast<int>((index >> 21) & 0x1FFFFFU),
              static_cast<int>((index >> 42) & 0x1FFFFFU)},
             color.color});
    }
    return result;
}

} // namespace engine::world::storage
