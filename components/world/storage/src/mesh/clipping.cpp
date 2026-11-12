#include "clipping.hpp"

#include <cmath>

namespace engine::world::storage {
namespace {

constexpr double kEpsilon = 1.0 / 65536;

struct Plane {
    unsigned axis = 0;
    int position = 0;
    bool upper = false;
};

struct Classification {
    std::array<bool, 3> lower;
    std::array<bool, 3> planar;
    unsigned lower_count = 0;
    unsigned planar_count = 0;
};

Classification classify(const Fragment& fragment, Plane plane) {
    Classification result;
    for (unsigned index = 0; index < 3; ++index) {
        const auto value = fragment.points[index][plane.axis];
        result.lower[index] = value < plane.position;
        result.planar[index] = std::abs(value - plane.position) < kEpsilon;
        result.lower_count += result.lower[index];
        result.planar_count += result.planar[index];
    }
    return result;
}

unsigned first(std::array<bool, 3> values, bool match = true) {
    for (unsigned index = 0; index < 3; ++index) {
        if (values[index] == match) {
            return index;
        }
    }
    return 2;
}

TextureCoordinate mix(TextureCoordinate a, TextureCoordinate b, double ratio) {
    return {a.u * (1 - ratio) + b.u * ratio, a.v * (1 - ratio) + b.v * ratio};
}

struct Vertex {
    model::Vec3 point;
    TextureCoordinate coordinate;
};

Vertex vertex(const Fragment& fragment, unsigned index) {
    return {fragment.points[index], fragment.coordinates[index]};
}

Vertex intersection(Vertex a, Vertex b, Plane plane) {
    const auto distance = b.point[plane.axis] - a.point[plane.axis];
    const auto ratio =
        std::abs(distance) < kEpsilon ? 0 : (plane.position - a.point[plane.axis]) / distance;
    return {a.point * (1 - ratio) + b.point * ratio, mix(a.coordinate, b.coordinate, ratio)};
}

Fragment triangle(Vertex a, Vertex b, Vertex c) {
    return {{a.point, b.point, c.point}, {a.coordinate, b.coordinate, c.coordinate}};
}

void append(std::vector<Fragment>& result, const Fragment& fragment, bool lower, Plane plane) {
    if (lower != plane.upper) {
        result.push_back(fragment);
    }
}

void one_planar(const Fragment& fragment, Classification classification, Plane plane,
                std::vector<Fragment>& result) {
    const auto planar = first(classification.planar);
    const auto a = (planar + 1) % 3;
    const auto b = (planar + 2) % 3;
    if (classification.lower[a] == classification.lower[b]) {
        append(result, fragment, classification.lower[a], plane);
        return;
    }
    const auto cut = intersection(vertex(fragment, a), vertex(fragment, b), plane);
    append(result, triangle(vertex(fragment, planar), vertex(fragment, a), cut),
           classification.lower[a], plane);
    append(result, triangle(vertex(fragment, planar), cut, vertex(fragment, b)),
           classification.lower[b], plane);
}

void regular_split(const Fragment& fragment, Classification classification, Plane plane,
                   std::vector<Fragment>& result) {
    const auto isolated_lower = classification.lower_count == 1;
    const auto isolated = first(classification.lower, isolated_lower);
    const auto a = vertex(fragment, (isolated + 1) % 3);
    const auto b = vertex(fragment, (isolated + 2) % 3);
    const auto origin = vertex(fragment, isolated);
    const auto cut_a = intersection(origin, a, plane);
    const auto cut_b = intersection(origin, b, plane);
    append(result, triangle(origin, cut_a, cut_b), isolated_lower, plane);
    append(result, triangle(cut_a, a, b), !isolated_lower, plane);
    append(result, triangle(cut_a, cut_b, b), !isolated_lower, plane);
}

void split(const Fragment& fragment, Plane plane, std::vector<Fragment>& result) {
    const auto classification = classify(fragment, plane);
    if (classification.lower_count == 0 || classification.lower_count == 3) {
        append(result, fragment, classification.lower_count == 3, plane);
    } else if (classification.planar_count == 3) {
        append(result, fragment, false, plane);
    } else if (classification.planar_count == 2) {
        append(result, fragment, classification.lower[first(classification.planar, false)], plane);
    } else if (classification.planar_count == 1) {
        one_planar(fragment, classification, plane, result);
    } else {
        regular_split(fragment, classification, plane, result);
    }
}

WeightedCoordinate collect(std::span<const Fragment> fragments, float original_area) {
    WeightedCoordinate result;
    for (const auto& fragment : fragments) {
        const auto& uv = fragment.coordinates;
        const TextureCoordinate center{(uv[0].u + uv[1].u + uv[2].u) / 3,
                                       (uv[0].v + uv[1].v + uv[2].v) / 3};
        const auto weight = result.weight + original_area;
        result.coordinate = mix(result.coordinate, center, original_area / weight);
        result.weight = weight;
    }
    return result;
}

} // namespace

WeightedCoordinate clip_voxel(const Fragment& fragment, model::Vec3i voxel, float original_area,
                              ClipWorkspace& workspace) {
    workspace.current.clear();
    workspace.current.push_back(fragment);
    const std::array<int, 3> position{voxel.x, voxel.y, voxel.z};
    for (unsigned side = 0; side < 2; ++side) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            workspace.next.clear();
            const Plane plane{axis, position[axis] + static_cast<int>(side), side == 0};
            for (const auto& part : workspace.current) {
                split(part, plane, workspace.next);
            }
            workspace.current.swap(workspace.next);
            if (workspace.current.empty()) {
                return {};
            }
        }
    }
    // The original 1.3.5-dev converter weights each clipped triangle by the
    // original face area; retain that behavior, including equal-weight ties.
    return collect(workspace.current, original_area);
}

} // namespace engine::world::storage
