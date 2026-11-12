#include "groups.hpp"

#include <limits>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {

using Position = world::model::Vec3i;
constexpr std::uint32_t kUnvisited = 0x3FFFFFFFU;

std::uint64_t volume(Position size) {
    return static_cast<std::uint64_t>(size.x) * size.y * size.z;
}

Position position(std::uint32_t index, Position size) {
    return {static_cast<int>(index % size.x), static_cast<int>((index / size.x) % size.y),
            static_cast<int>(index / size.x / size.y)};
}

std::uint32_t linear(Position point, Position size) {
    return static_cast<std::uint32_t>(point.x + static_cast<std::uint64_t>(point.y) * size.x +
                                      static_cast<std::uint64_t>(point.z) * size.x * size.y);
}

bool contains(Position size, Position point) {
    return point.x >= 0 && point.y >= 0 && point.z >= 0 && point.x < size.x && point.y < size.y &&
           point.z < size.z;
}

std::uint32_t packed(Position point) {
    return static_cast<std::uint32_t>(point.x) | (static_cast<std::uint32_t>(point.y) << 11U) |
           (static_cast<std::uint32_t>(point.z) << 21U);
}

std::uint32_t matches(std::uint32_t neighbor, std::uint32_t current) {
    std::uint32_t result = 0;
    for (unsigned side = 0; side < 6; ++side) {
        result |= (((neighbor >> (side * 5)) & 31U) >= ((current >> (side * 5)) & 31U)) << side;
    }
    return result;
}

void grow(std::uint32_t neighbor, unsigned side, std::uint32_t& current) {
    constexpr std::array<unsigned, 6> kMasks{0x3DU, 0x3EU, 0x37U, 0x3BU, 0x1FU, 0x2FU};
    if ((neighbor & 0x80000000U) == 0 &&
        (matches(neighbor, current) & kMasks[side]) == kMasks[side]) {
        current += 4U << (side * 5);
    }
}

} // namespace

Position aadf_group_dimensions(Position chunks) {
    if (chunks.x <= 0 || chunks.y <= 0 || chunks.z <= 0 || chunks.x > 2048 || chunks.y > 1024 ||
        chunks.z > 2048) {
        throw std::invalid_argument("World chunks exceed the original 11/10/11 coordinate packing");
    }
    const Position groups{(chunks.x + 3) / 4, (chunks.y + 3) / 4, (chunks.z + 3) / 4};
    if (volume(groups) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Too many original AADF queue groups");
    }
    return groups;
}

void GroupChanges::initialize(Position chunks) {
    groups_ = aadf_group_dimensions(chunks);
    chunks_ = chunks;
    distances_.assign(static_cast<std::size_t>(volume(groups_)), kUnvisited);
    changed_.clear();
    pending_.clear();
    records_.clear();
}

Position GroupChanges::dimensions() const {
    return groups_;
}

std::uint32_t GroupChanges::count() const {
    return static_cast<std::uint32_t>(distances_.size());
}

void GroupChanges::mark(std::uint32_t chunk) {
    if (chunk >= volume(chunks_)) {
        throw std::out_of_range("Changed chunk lies outside the world");
    }
    const auto point = position(chunk, chunks_);
    const auto group = linear({point.x / 4, point.y / 4, point.z / 4}, groups_);
    if (distances_[group] != kUnvisited) {
        return;
    }
    distances_[group] = 0x80000000U;
    changed_.push_back(group);
    pending_.push_back(group);
}

void GroupChanges::expand(std::uint32_t group) {
    const auto point = position(group, groups_);
    const auto distance = distances_[group] & 0x7FFFFFFFU;
    for (int index = 0; index < 27; ++index) {
        const Position next{point.x + index % 3 - 1, point.y + (index / 3) % 3 - 1,
                            point.z + index / 9 - 1};
        if (!contains(groups_, next) || next == point) {
            continue;
        }
        const auto neighbor = linear(next, groups_);
        if (distances_[neighbor] != kUnvisited) {
            continue;
        }
        distances_[neighbor] = distance + 4;
        changed_.push_back(neighbor);
        if (distance < 28) {
            pending_.push_back(neighbor);
        }
    }
}

void GroupChanges::refine_axis(std::size_t original_count, unsigned axis) {
    const std::array<int, 3> dimensions{groups_.x, groups_.y, groups_.z};
    const std::array<std::uint32_t, 3> strides{1U, static_cast<std::uint32_t>(groups_.x),
                                               static_cast<std::uint32_t>(groups_.x * groups_.y)};
    for (auto item = original_count; item < changed_.size(); ++item) {
        const auto index = changed_[item];
        const auto point = position(index, groups_);
        const std::array<int, 3> coordinate{point.x, point.y, point.z};
        auto current = distances_[index];
        if (coordinate[axis] > 0) {
            grow(distances_[index - strides[axis]], axis * 2, current);
        } else {
            current += 4U << (axis * 10);
        }
        if (coordinate[axis] + 1 < dimensions[axis]) {
            grow(distances_[index + strides[axis]], axis * 2 + 1, current);
        } else {
            current += 4U << (axis * 10 + 5);
        }
        distances_[index] = current;
    }
}

void GroupChanges::refine(std::size_t original_count) {
    for (auto item = original_count; item < changed_.size(); ++item) {
        distances_[changed_[item]] = 0;
    }
    for (unsigned iteration = 0; iteration < 7; ++iteration) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            refine_axis(original_count, axis);
        }
    }
}

void GroupChanges::emit(std::size_t original_count) {
    records_.reserve(changed_.size());
    for (std::size_t index = 0; index < changed_.size(); ++index) {
        const auto group = changed_[index];
        const auto distance = index < original_count ? 0xC0000000U : distances_[group];
        records_.push_back({packed(position(group, groups_)), distance});
        distances_[group] = kUnvisited;
    }
}

std::span<const GroupRecord> GroupChanges::build(std::span<const std::uint32_t> chunks) {
    changed_.clear();
    records_.clear();
    for (const auto chunk : chunks) {
        mark(chunk);
    }
    const auto original_count = changed_.size();
    while (!pending_.empty()) {
        const auto group = pending_.front();
        pending_.pop_front();
        expand(group);
    }
    refine(original_count);
    emit(original_count);
    return records_;
}

} // namespace engine::rendering::vulkan
