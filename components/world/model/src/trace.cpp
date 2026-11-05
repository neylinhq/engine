#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::world::model {
namespace {

struct Cell {
    Vec3 lower;
    Vec3 upper;
    std::uint16_t material = 0;
};

struct TraceState {
    Vec3 origin;
    Vec3 direction;
    Vec3 normal;
    double distance = 0;
    double end = 0;
};

Cell box_cell(IVec3 position, int width, std::uint32_t word, unsigned bits) {
    Cell result;
    const std::array<int, 3> coordinates{position.x, position.y, position.z};
    const auto mask = (1U << bits) - 1;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto low = (word >> (2 * axis * bits)) & mask;
        const auto high = (word >> ((2 * axis + 1) * bits)) & mask;
        const auto start = (coordinates[axis] / width) * width;
        result.lower[axis] = start - static_cast<double>(low) * width;
        result.upper[axis] = start + static_cast<double>(high + 1) * width;
    }
    return result;
}

Cell node_cell(const Model& model, IVec3 position) {
    const IVec3 chunk{position.x / 16, position.y / 16, position.z / 16};
    auto word = model.chunks[flatten(chunk, chunk_size(model.size))];
    if ((word >> 30) == 0) {
        return box_cell(position, 16, word, 5);
    }
    if ((word >> 30) == 1) {
        return {{}, {}, static_cast<std::uint16_t>(word & 0x7FFFU)};
    }
    const IVec3 block{position.x % 16 / 4, position.y % 16 / 4, position.z % 16 / 4};
    word = model.blocks[(word & 0x3FFFFFFFU) + flatten(block, {4, 4, 4})];
    if ((word >> 30) == 0) {
        return box_cell(position, 4, word, 2);
    }
    if ((word >> 30) == 1) {
        return {{}, {}, static_cast<std::uint16_t>(word & 0x7FFFU)};
    }
    const auto index = flatten({position.x % 4, position.y % 4, position.z % 4}, {4, 4, 4});
    word = (model.voxels[(word & 0x3FFFFFFFU) + index / 2] >> (16 * (index & 1))) & 0xFFFFU;
    if ((word & 0x8000U) != 0) {
        return {{}, {}, static_cast<std::uint16_t>(word & 0x7FFFU)};
    }
    return box_cell(position, 1, word, 2);
}

bool enter_world(TraceState& trace, IVec3 size) {
    const std::array<int, 3> dimensions{size.x, size.y, size.z};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto direction = trace.direction[axis];
        if (std::abs(direction) < 1e-15) {
            if (trace.origin[axis] < 0 || trace.origin[axis] >= dimensions[axis]) {
                return false;
            }
            continue;
        }
        auto near = -trace.origin[axis] / direction;
        auto far = (dimensions[axis] - trace.origin[axis]) / direction;
        if (near > far) {
            std::swap(near, far);
        }
        if (near >= trace.distance) {
            trace.distance = near;
            trace.normal = {};
            trace.normal[axis] = direction > 0 ? -1 : 1;
        }
        trace.end = std::min(trace.end, far);
    }
    return trace.end >= trace.distance;
}

void step_cell(TraceState& trace, const Cell& cell) {
    double next = std::numeric_limits<double>::infinity();
    unsigned selected = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (std::abs(trace.direction[axis]) < 1e-15) {
            continue;
        }
        const auto face = trace.direction[axis] > 0 ? cell.upper[axis] : cell.lower[axis];
        const auto distance = (face - trace.origin[axis]) / trace.direction[axis];
        if (distance < next) {
            next = distance;
            selected = axis;
        }
    }
    trace.distance = std::max(next, trace.distance + 1e-7);
    trace.normal = {};
    trace.normal[selected] = trace.direction[selected] > 0 ? -1 : 1;
}

} // namespace

Hit World::trace(Vec3 origin, Vec3 direction, double max_distance) const {
    Hit result;
    if (!foundation::geometry::finite(origin) || !foundation::geometry::finite(direction)) {
        return result;
    }
    direction = foundation::geometry::normalized(direction);
    if (foundation::geometry::length(direction) == 0) {
        return result;
    }
    TraceState trace{origin, direction, {}, 0, max_distance};
    if (!enter_world(trace, size())) {
        return result;
    }
    for (unsigned step = 0; step < 1000 && trace.distance <= trace.end; ++step) {
        const auto point = origin + direction * (trace.distance + 1e-7);
        const IVec3 voxel{static_cast<int>(std::floor(point.x)),
                          static_cast<int>(std::floor(point.y)),
                          static_cast<int>(std::floor(point.z))};
        if (!inside(voxel, size())) {
            break;
        }
        const auto cell = node_cell(state_->model, voxel);
        result.steps = step + 1;
        if (cell.material != 0) {
            return {true, trace.distance, voxel, trace.normal, cell.material, step + 1};
        }
        step_cell(trace, cell);
    }
    return result;
}

} // namespace engine::world::model
