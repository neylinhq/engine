#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace engine::world::model {
namespace {

void validate_position(Vec3 position) {
    if (!foundation::geometry::finite(position) || position.x < 0 || position.y < 0 ||
        position.z < 0 || position.x >= 16384 || position.y >= 4096 || position.z >= 16384) {
        throw std::runtime_error("Entity position exceeds original packed coordinate range");
    }
}

std::array<std::uint32_t, 4> history_record(const EntityInstance& instance) {
    const auto x = static_cast<std::uint32_t>(instance.position.x * 128);
    const auto y = static_cast<std::uint32_t>(instance.position.y * 128);
    const auto z = static_cast<std::uint32_t>(instance.position.z * 128);
    const auto quaternion = pack_quaternion(instance.quaternion);
    return {x | ((y & 0x7FFU) << 21), z | ((y >> 11) << 21), quaternion[0], quaternion[1]};
}

std::array<std::uint32_t, 5> instance_record(const EntityInstance& instance, const EntityType& type,
                                             std::uint32_t index) {
    auto inverse = instance.quaternion;
    inverse[0] = -inverse[0];
    inverse[1] = -inverse[1];
    inverse[2] = -inverse[2];
    const auto quaternion = pack_quaternion(inverse);
    auto history = history_record(instance);
    return {history[0],
            history[1] | (((static_cast<std::uint32_t>(type.size.z) & 127U) >> 4) << 29),
            quaternion[0], quaternion[1] | (type.voxel_start << 12),
            index | ((static_cast<std::uint32_t>(type.size.x) & 127U) << 14) |
                ((static_cast<std::uint32_t>(type.size.y) & 127U) << 21) |
                ((static_cast<std::uint32_t>(type.size.z) & 15U) << 28)};
}

foundation::geometry::Box instance_bounds(const EntityInstance& instance, IVec3 size) {
    foundation::geometry::Box result{{1e30, 1e30, 1e30}, {-1e30, -1e30, -1e30}};
    for (unsigned corner = 0; corner < 8; ++corner) {
        const Vec3 point{(corner & 1) != 0 ? static_cast<double>(size.x) : 0,
                         (corner & 2) != 0 ? static_cast<double>(size.y) : 0,
                         (corner & 4) != 0 ? static_cast<double>(size.z) : 0};
        const auto transformed = rotate(point, instance.quaternion) + instance.position;
        for (unsigned axis = 0; axis < 3; ++axis) {
            result.lower[axis] = std::min(result.lower[axis], transformed[axis]);
            result.upper[axis] = std::max(result.upper[axis], transformed[axis]);
        }
    }
    return result;
}

using ChunkLists = std::map<std::size_t, std::vector<std::uint32_t>>;

void add_to_chunks(ChunkLists& lists, foundation::geometry::Box box, IVec3 size,
                   std::uint32_t index) {
    const IVec3 lower{std::max(0, static_cast<int>(std::floor(box.lower.x / 16))),
                      std::max(0, static_cast<int>(std::floor(box.lower.y / 16))),
                      std::max(0, static_cast<int>(std::floor(box.lower.z / 16)))};
    const IVec3 upper{std::min(size.x - 1, static_cast<int>(std::floor(box.upper.x / 16))),
                      std::min(size.y - 1, static_cast<int>(std::floor(box.upper.y / 16))),
                      std::min(size.z - 1, static_cast<int>(std::floor(box.upper.z / 16)))};
    const IVec3 extent{upper.x - lower.x + 1, upper.y - lower.y + 1, upper.z - lower.z + 1};
    if (extent.x <= 0 || extent.y <= 0 || extent.z <= 0) {
        return;
    }
    for (std::size_t cell = 0; cell < volume(extent); ++cell) {
        const auto offset = unflatten(cell, extent);
        const IVec3 position{lower.x + offset.x, lower.y + offset.y, lower.z + offset.z};
        auto& list = lists[flatten(position, size)];
        if (list.size() == 255) {
            throw std::runtime_error("More than 255 entities overlap a chunk");
        }
        list.push_back(index);
    }
}

struct ChunkUpdate {
    std::vector<std::size_t> occupancy;
    bool changed = false;
};

using SharedLists = std::map<std::vector<std::uint32_t>, std::uint32_t>;

std::uint32_t packed_list(EntityGpuData& data, SharedLists& shared,
                          const std::vector<std::uint32_t>& list,
                          std::span<const std::array<std::uint32_t, 5>> records) {
    if (auto found = shared.find(list); found != shared.end()) {
        return found->second;
    }
    if (data.instances.size() + list.size() >= (1U << 24)) {
        throw std::runtime_error("Entity chunk lists exceed 24-bit address range");
    }
    const auto packed = static_cast<std::uint32_t>((data.instances.size() << 8) | list.size());
    shared.emplace(list, packed);
    for (auto index : list) {
        data.instances.push_back(records[index]);
    }
    return packed;
}

ChunkUpdate remove_old_lists(EntityGpuData& data, const ChunkLists& lists,
                             std::span<const std::size_t> previous_chunks) {
    ChunkUpdate update;
    for (auto chunk : previous_chunks) {
        if (!lists.contains(chunk)) {
            data.chunks[chunk] = 0;
            data.changed_chunks.push_back(static_cast<std::uint32_t>(chunk));
            update.occupancy.push_back(chunk);
            update.changed = true;
        }
    }
    return update;
}

ChunkUpdate compress_lists(EntityGpuData& data, const ChunkLists& lists,
                           std::span<const std::array<std::uint32_t, 5>> records,
                           std::span<const std::size_t> previous_chunks) {
    SharedLists shared;
    const auto previous = std::move(data.instances);
    data.instances = {};
    auto update = remove_old_lists(data, lists, previous_chunks);
    for (const auto& [chunk, list] : lists) {
        const auto packed = packed_list(data, shared, list, records);
        if (data.chunks[chunk] == 0) {
            update.occupancy.push_back(chunk);
        }
        if (data.chunks[chunk] != packed) {
            update.changed = true;
            data.changed_chunks.push_back(static_cast<std::uint32_t>(chunk));
        }
        data.chunks[chunk] = packed;
    }
    if (data.instances != previous) {
        ++data.revision;
    }
    return update;
}

} // namespace

std::span<const EntityType> World::entity_types() const {
    return state_->entity_types;
}

std::span<const EntityInstance> World::entities() const {
    return state_->entities;
}

std::size_t World::create_entity_type(std::size_t model_index) {
    const auto& model = state_->models.at(model_index);
    if (model.size.x > 128 || model.size.y > 128 || model.size.z > 128) {
        throw std::runtime_error("Entity dimensions must be at most 128");
    }
    EntityType type{model.name, model.size, entity_voxels(model), 0};
    auto& voxels = state_->entity_gpu.voxels;
    type.voxel_start = static_cast<std::uint32_t>((voxels.size() + 63) / 64);
    if (type.voxel_start >= (1U << 20)) {
        throw std::runtime_error("Entity voxel data exceeds packed pointer range");
    }
    voxels.resize(static_cast<std::size_t>(type.voxel_start) * 64);
    voxels.insert(voxels.end(), type.voxels.begin(), type.voxels.end());
    ++state_->entity_gpu.voxel_revision;
    state_->entity_types.push_back(std::move(type));
    return state_->entity_types.size() - 1;
}

std::size_t World::spawn_entity(std::size_t type_index, Vec3 position) {
    validate_position(position);
    if (type_index >= state_->entity_types.size() || state_->entities.size() >= 16383) {
        throw std::runtime_error("Invalid entity type or too many entity instances");
    }
    state_->entities.push_back({static_cast<std::uint32_t>(type_index), position});
    state_->entity_gpu.entity_count = static_cast<std::uint32_t>(state_->entities.size());
    return state_->entities.size() - 1;
}

void World::set_entity_position(std::size_t index, Vec3 position) {
    validate_position(position);
    state_->entities.at(index).position = position;
}

void World::remove_entities_from(std::size_t index) {
    if (index > state_->entities.size()) {
        throw std::runtime_error("Invalid entity removal range");
    }
    state_->entities.resize(index);
    state_->entity_gpu.entity_count = static_cast<std::uint32_t>(state_->entities.size());
}

void World::clear_entities() {
    state_->entities.clear();
    state_->entity_gpu.entity_count = 0;
    state_->entity_gpu.instances.clear();
    for (auto chunk : state_->entity_chunks) {
        state_->entity_gpu.chunks[chunk] = 0;
        state_->entity_gpu.changed_chunks.push_back(static_cast<std::uint32_t>(chunk));
    }
    state_->invalidate(state_->entity_chunks);
    unique_indices(state_->entity_gpu.changed_chunks);
    state_->entity_chunks.clear();
    std::ranges::fill(state_->entity_gpu.history, std::array<std::uint32_t, 4>{});
    ++state_->entity_gpu.revision;
    ++state_->chunk_revision;
}

void World::update_entities(double seconds, double rotation_speed, unsigned history_index) {
    if (history_index >= 128 || !std::isfinite(seconds) || !std::isfinite(rotation_speed)) {
        throw std::runtime_error("Invalid entity animation time or history index");
    }
    state_->entity_time += std::max(0.0, seconds);
    const auto angle = 2 + state_->entity_time * rotation_speed / 4;
    auto& data = state_->entity_gpu;
    ChunkLists lists;
    std::vector<std::array<std::uint32_t, 5>> records;
    if (!state_->entities.empty()) {
        data.history.resize(16384U * 128U);
    }
    for (std::size_t index = 0; index < state_->entities.size(); ++index) {
        auto& instance = state_->entities[index];
        instance.quaternion = {0, static_cast<float>(std::sin(angle / 2)), 0,
                               static_cast<float>(std::cos(angle / 2))};
        const auto& type = state_->entity_types[instance.type];
        add_to_chunks(lists, instance_bounds(instance, type.size), chunk_size(size()),
                      static_cast<std::uint32_t>(index));
        records.push_back(instance_record(instance, type, static_cast<std::uint32_t>(index)));
        data.history[history_index * 16384U + index] = history_record(instance);
    }
    const auto update = compress_lists(data, lists, records, state_->entity_chunks);
    unique_indices(data.changed_chunks);
    if (update.changed) {
        ++state_->chunk_revision;
    }
    state_->invalidate(update.occupancy);
    state_->entity_chunks.clear();
    for (const auto& [index, list] : lists) {
        static_cast<void>(list);
        state_->entity_chunks.push_back(index);
    }
}

const EntityGpuData& World::entity_gpu() const {
    return state_->entity_gpu;
}

} // namespace engine::world::model
