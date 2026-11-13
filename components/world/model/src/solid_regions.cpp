#include <engine/world/model/world.hpp>

#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::world::model {
namespace {

using foundation::geometry::Box;

constexpr int kChunkWidth = 16;
constexpr int kBlockWidth = 4;
constexpr Vec3i kBlockDimensions{4, 4, 4};
constexpr Vec3i kChunkDimensions{16, 16, 16};
constexpr auto kPointerMask = 0x3FFFFFFFU;

struct Region {
    Vec3i begin;
    Vec3i end;
};

Box bounds_at(Vec3i origin, int width) {
    const Vec3 lower{double(origin.x), double(origin.y), double(origin.z)};
    const auto edge = static_cast<double>(width);
    return {lower, lower + Vec3{edge, edge, edge}};
}

struct Visit {
    const WorldView& world;
    WorldView::SolidVisitor visitor;
    void* context;

    bool solid(Vec3i origin, int width) const {
        return visitor(context, bounds_at(origin, width));
    }
};

Region cell_region(Box bounds, Vec3i size) {
    return {{static_cast<int>(std::clamp(std::floor(bounds.lower.x), 0.0, double(size.x))),
             static_cast<int>(std::clamp(std::floor(bounds.lower.y), 0.0, double(size.y))),
             static_cast<int>(std::clamp(std::floor(bounds.lower.z), 0.0, double(size.z)))},
            {static_cast<int>(std::clamp(std::ceil(bounds.upper.x), 0.0, double(size.x))),
             static_cast<int>(std::clamp(std::ceil(bounds.upper.y), 0.0, double(size.y))),
             static_cast<int>(std::clamp(std::ceil(bounds.upper.z), 0.0, double(size.z)))}};
}

Region clipped(Region region, Vec3i origin, int width) {
    return {{std::max(region.begin.x, origin.x), std::max(region.begin.y, origin.y),
             std::max(region.begin.z, origin.z)},
            {std::min(region.end.x, origin.x + width), std::min(region.end.y, origin.y + width),
             std::min(region.end.z, origin.z + width)}};
}

bool empty(Region region) {
    return region.begin.x >= region.end.x || region.begin.y >= region.end.y ||
           region.begin.z >= region.end.z;
}

template <typename Visitor> bool visit_cells(Region region, int width, Visitor visitor) {
    if (empty(region)) {
        return false;
    }
    for (int z = region.begin.z / width * width; z < region.end.z; z += width) {
        for (int y = region.begin.y / width * width; y < region.end.y; y += width) {
            for (int x = region.begin.x / width * width; x < region.end.x; x += width) {
                if (visitor(Vec3i{x, y, z})) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool occupied_node(std::uint32_t word) {
    return (word >> 30) == 1 && (word & 0x7FFFU) != 0;
}

bool visit_voxels(Visit visit, Region region, Vec3i origin, std::uint32_t offset) {
    return visit_cells(clipped(region, origin, kBlockWidth), 1, [&](Vec3i voxel) {
        const auto local =
            flatten({voxel.x - origin.x, voxel.y - origin.y, voxel.z - origin.z}, kBlockDimensions);
        const auto value = (visit.world.voxels[offset + local / 2] >> (16 * (local & 1))) & 0xFFFFU;
        return (value & 0x8000U) != 0 && (value & 0x7FFFU) != 0 && visit.solid(voxel, 1);
    });
}

bool visit_blocks(Visit visit, Region region, Vec3i origin, std::uint32_t offset) {
    return visit_cells(clipped(region, origin, kChunkWidth), kBlockWidth, [&](Vec3i block) {
        const Vec3i local{(block.x - origin.x) / kBlockWidth, (block.y - origin.y) / kBlockWidth,
                          (block.z - origin.z) / kBlockWidth};
        const auto word = visit.world.blocks[offset + flatten(local, kBlockDimensions)];
        if ((word >> 30) == 2) {
            return visit_voxels(visit, region, block, word & kPointerMask);
        }
        return occupied_node(word) && visit.solid(block, kBlockWidth);
    });
}

struct PlacementCheck {
    const Model& model;
    Region region;
    PlacementGuard guard;
};

bool blocks_chunk(const PlacementCheck& check, std::size_t index, Vec3i origin,
                  const ChunkEdit& edit) {
    const auto region = clipped(check.region, origin, kChunkWidth);
    if (empty(region) || (!edit.voxels && edit.material == 0)) {
        return false;
    }
    const auto before = decode_chunk(check.model, index);
    return visit_cells(region, 1, [&](Vec3i voxel) {
        const auto local =
            flatten({voxel.x - origin.x, voxel.y - origin.y, voxel.z - origin.z}, kChunkDimensions);
        const auto material = edit.voxels ? (*edit.voxels)[local] : edit.material;
        return material != 0 && before[local] == 0 &&
               check.guard.blocks(bounds_at(voxel, 1), check.guard.context);
    });
}

} // namespace

bool World::blocks_placement(PlacementGuard guard) const {
    const auto [lower, upper] = guard.bounds;
    if (!finite(lower) || !finite(upper) || lower.x >= upper.x || lower.y >= upper.y ||
        lower.z >= upper.z) {
        return true;
    }
    const auto region = cell_region(guard.bounds, size());
    if (empty(region) || state_->edits.empty()) {
        return false;
    }
    const auto dimensions = chunk_size(size());
    const PlacementCheck check{state_->model, region, guard};
    return visit_cells(region, kChunkWidth, [&](Vec3i origin) {
        const Vec3i chunk{origin.x / kChunkWidth, origin.y / kChunkWidth, origin.z / kChunkWidth};
        const auto index = flatten(chunk, dimensions);
        const auto edit = state_->edits.find(index);
        return edit != state_->edits.end() && blocks_chunk(check, index, origin, edit->second);
    });
}

bool WorldView::visit_solids(Box bounds, SolidVisitor visitor, void* context) const {
    if (!visitor || chunks.empty() || !finite(bounds.lower) || !finite(bounds.upper) ||
        bounds.lower.x >= bounds.upper.x || bounds.lower.y >= bounds.upper.y ||
        bounds.lower.z >= bounds.upper.z) {
        return false;
    }
    const auto dimensions = chunk_size(size);
    const auto region = cell_region(bounds, size);
    const Visit visit{*this, visitor, context};
    return visit_cells(region, kChunkWidth, [&](Vec3i origin) {
        const Vec3i chunk{origin.x / kChunkWidth, origin.y / kChunkWidth, origin.z / kChunkWidth};
        const auto word = chunks[flatten(chunk, dimensions)];
        if ((word >> 30) == 2) {
            return visit_blocks(visit, region, origin, word & kPointerMask);
        }
        return occupied_node(word) && visit.solid(origin, kChunkWidth);
    });
}

} // namespace engine::world::model
