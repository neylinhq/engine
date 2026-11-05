#pragma once

#include "../frame/values.hpp"
#include "../world/compute.hpp"
#include "groups.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace engine::rendering::vulkan {

inline void validate_aadf_capacity(const Device& device, world::model::IVec3 chunks) {
    const auto groups = aadf_group_dimensions(chunks);
    const auto count = std::uint64_t(groups.x) * groups.y * groups.z;
    const auto bytes = count * 96U * sizeof(std::uint32_t);
    const auto maximum = device.properties.limits.maxStorageBufferRange;
    if (bytes > maximum) {
        throw std::runtime_error("AADF queue requires " + std::to_string(bytes) +
                                 " bytes; device maxStorageBufferRange is " +
                                 std::to_string(maximum) + " bytes");
    }
}

[[nodiscard]] inline std::uint32_t update_count(std::size_t count) {
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Original world update record count exceeds 32 bits");
    }
    return static_cast<std::uint32_t>(count);
}

[[nodiscard]] inline vk::Extent3D update_grid(std::uint32_t groups) {
    return {std::min(groups, 32768U), (groups + 32767U) / 32768U, 1};
}

[[nodiscard]] inline UniformValues update_values(world::model::IVec3 chunks,
                                                 const GroupChanges& changes) {
    const auto groups = changes.dimensions();
    UniformValues values;
    set_value(values, "chunkSizeX", chunks.x);
    set_value(values, "chunkSizeY", chunks.y);
    set_value(values, "chunkSizeZ", chunks.z);
    set_value(values, "groupSizeX", groups.x);
    set_value(values, "groupSizeY", groups.y);
    set_value(values, "groupSizeZ", groups.z);
    set_value(values, "boundGroupQueueMaxSize", changes.count());
    return values;
}

[[nodiscard]] inline std::vector<GroupRecord> chunk_records(std::span<const std::uint32_t> indices,
                                                            std::span<const std::uint32_t> words,
                                                            world::model::IVec3 size) {
    std::vector<GroupRecord> records;
    records.reserve(indices.size());
    for (const auto index : indices) {
        if (index >= words.size()) {
            throw std::out_of_range("Original chunk update exceeds CPU mirror");
        }
        const auto x = index % size.x;
        const auto y = (index / size.x) % size.y;
        const auto z = index / size.x / size.y;
        records.push_back({x | (y << 11U) | (z << 21U), words[index]});
    }
    return records;
}

template <std::size_t width>
[[nodiscard]] std::vector<std::array<std::uint32_t, width + 1>>
payload_records(std::span<const std::uint32_t> offsets, std::span<const std::uint32_t> words) {
    std::vector<std::array<std::uint32_t, width + 1>> records(offsets.size());
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        const auto offset = offsets[index];
        if (offset > words.size() || width > words.size() - offset) {
            throw std::out_of_range("Original packed update exceeds CPU mirror");
        }
        records[index][0] = offset;
        std::copy_n(words.begin() + offset, width, records[index].begin() + 1);
    }
    return records;
}

} // namespace engine::rendering::vulkan
