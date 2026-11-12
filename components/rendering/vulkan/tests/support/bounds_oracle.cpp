#include "bounds_oracle.hpp"

#include <algorithm>
#include <set>

namespace engine::rendering::vulkan::test {
namespace {

using Cell = std::array<std::uint32_t, 64>;
using Point = std::array<int, 3>;

Point coordinates(std::uint32_t index, world::model::Vec3i size) {
    return {static_cast<int>(index % size.x), static_cast<int>((index / size.x) % size.y),
            static_cast<int>(index / size.x / size.y)};
}

void check_cell(const Cell& cells, std::uint32_t bound, std::uint32_t index, unsigned shift) {
    const auto point = coordinates(index, {4, 4, 4});
    Point low{}, high{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        low[axis] = point[axis] - static_cast<int>((bound >> (axis * 4)) & 3U);
        high[axis] = point[axis] + static_cast<int>((bound >> (axis * 4 + 2)) & 3U);
        require_update(low[axis] >= 0 && high[axis] < 4,
                       "Mutation local AADF crosses its 4x4x4 boundary");
    }
    for (std::uint32_t candidate = 0; candidate < cells.size(); ++candidate) {
        const auto other = coordinates(candidate, {4, 4, 4});
        bool inside = true;
        for (unsigned axis = 0; axis < 3; ++axis) {
            inside = inside && other[axis] >= low[axis] && other[axis] <= high[axis];
        }
        require_update(!inside || (cells[candidate] >> shift) == 0,
                       "Mutation local AADF skips an occupied cell");
    }
}

void check_local(const Cell& raw, const Cell& gpu, unsigned shift) {
    for (std::uint32_t index = 0; index < raw.size(); ++index) {
        if ((raw[index] >> shift) != 0) {
            require_update(raw[index] == gpu[index], "Mutation AADF changed occupied data");
        } else {
            require_update(raw[index] == 0, "CPU mutation mirror contains precomputed AADF");
            require_update((gpu[index] >> shift) == 0, "Mutation AADF changed occupancy");
            check_cell(raw, gpu[index], index, shift);
        }
    }
}

Cell block_cells(std::span<const std::uint32_t> words, std::uint32_t offset) {
    Cell result{};
    require_update(offset + 64U <= words.size(), "GPU chunk block pointer exceeds buffer");
    std::copy_n(words.begin() + offset, 64, result.begin());
    return result;
}

Cell voxel_cells(std::span<const std::uint32_t> words, std::uint32_t offset) {
    Cell result{};
    require_update(offset + 32U <= words.size(), "GPU block voxel pointer exceeds buffer");
    for (unsigned index = 0; index < result.size(); ++index) {
        result[index] = (words[offset + index / 2] >> ((index % 2) * 16)) & 0xFFFFU;
    }
    return result;
}

bool in_chunk_box(Point origin, std::uint32_t bounds, Point candidate) {
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto low = origin[axis] - static_cast<int>((bounds >> (axis * 10)) & 31U);
        const auto high = origin[axis] + static_cast<int>((bounds >> (axis * 10 + 5)) & 31U);
        if (candidate[axis] < low || candidate[axis] > high) {
            return false;
        }
    }
    return true;
}

} // namespace

void verify_local_bounds(const world::model::Model& raw, const world::model::Model& gpu) {
    std::set<std::uint32_t> block_ranges;
    std::set<std::uint32_t> voxel_ranges;
    for (const auto chunk : raw.chunks) {
        if ((chunk >> 30) == 2) {
            block_ranges.insert(chunk & 0x3FFFFFFFU);
        }
    }
    for (const auto offset : block_ranges) {
        const auto cells = block_cells(raw.blocks, offset);
        check_local(cells, block_cells(gpu.blocks, offset), 30);
        for (const auto block : cells) {
            if ((block >> 30) == 2) {
                voxel_ranges.insert(block & 0x3FFFFFFFU);
            }
        }
    }
    for (const auto offset : voxel_ranges) {
        check_local(voxel_cells(raw.voxels, offset), voxel_cells(gpu.voxels, offset), 15);
    }
}

void verify_chunk_bounds(world::model::Vec3i size, std::span<const GroupRecord> chunks) {
    std::vector<Point> occupied;
    for (std::uint32_t index = 0; index < chunks.size(); ++index) {
        if ((chunks[index][0] >> 30) != 0 || chunks[index][1] != 0) {
            occupied.push_back(coordinates(index, size));
        }
    }
    for (std::uint32_t index = 0; index < chunks.size(); ++index) {
        if ((chunks[index][0] >> 30) != 0 || chunks[index][1] != 0) {
            continue;
        }
        const auto point = coordinates(index, size);
        for (const auto candidate : occupied) {
            require_update(!in_chunk_box(point, chunks[index][0], candidate),
                           "GPU chunk AADF skips occupied geometry or an entity");
        }
    }
}

void verify_chunk_occupancy(const world::model::World& world, std::span<const GroupRecord> chunks) {
    const auto raw = world.view();
    const auto& entities = world.entity_gpu();
    require_update(raw.chunks.size() == chunks.size(), "GPU chunk dimensions changed");
    for (std::size_t index = 0; index < chunks.size(); ++index) {
        const auto expected = raw.chunks[index];
        require_update((chunks[index][0] >> 30) == (expected >> 30),
                       "Chunk scatter changed occupancy");
        if ((expected >> 30) != 0) {
            require_update(chunks[index][0] == expected, "Chunk scatter changed a packed pointer");
        }
        require_update(chunks[index][1] == entities.chunks[index],
                       "Entity chunk scatter differs from original CPU membership");
    }
}

void verify_queue_empty(WorldCompute& compute) {
    const auto words = update_words(compute, "boundQueueInfo", 96 * 2);
    for (std::size_t index = 1; index < words.size(); index += 2) {
        require_update(words[index] == 0, "GPU AADF queue did not drain");
    }
}

} // namespace engine::rendering::vulkan::test
