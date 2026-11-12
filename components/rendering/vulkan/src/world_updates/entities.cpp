#include "../world/updates.hpp"

#include "support.hpp"

namespace engine::rendering::vulkan {
namespace {

void update_entity_chunks(WorldCompute& compute, const world::model::EntityGpuData& data,
                          world::model::Vec3i chunks) {
    if (data.changed_chunks.empty()) {
        return;
    }
    const auto records = chunk_records(data.changed_chunks, data.chunks, chunks);
    UniformValues values;
    set_value(values, "updateCount", update_count(records.size()));
    compute.upload("chunkUpdatesDynamic", std::as_bytes(std::span(records)));
    compute.run("world_data_entityUpdate_updateChunks",
                update_grid((update_count(records.size()) + 63U) / 64U), values);
}

void copy_entity_instances(WorldCompute& compute, const world::model::EntityGpuData& data) {
    if (data.instances.empty()) {
        return;
    }
    const auto bytes = std::as_bytes(std::span(data.instances));
    compute.grow("entityChunkInstances", bytes.size());
    compute.upload("entityChunkInstancesDynamic", bytes);
    UniformValues values;
    set_value(values, "entityChunkInstanceCount", update_count(data.instances.size()));
    compute.run("world_data_entityUpdate_copyEntityChunkInstances",
                update_grid((update_count(data.instances.size()) + 63U) / 64U), values);
}

void copy_entity_history(WorldCompute& compute, const world::model::EntityGpuData& data,
                         unsigned history_index) {
    if (data.entity_count == 0) {
        return;
    }
    const auto first = static_cast<std::size_t>(history_index) * 16384U;
    if (history_index >= 128 || data.entity_count >= 16384 ||
        data.history.size() < first + data.entity_count) {
        throw std::out_of_range("Original entity history row exceeds its allocation");
    }
    const auto records = std::span(data.history).subspan(first, data.entity_count);
    compute.upload("entityHistoryDynamic", std::as_bytes(records));
    UniformValues values;
    set_value(values, "entityInstanceCount", data.entity_count);
    set_value(values, "taaIndex", history_index);
    compute.run("world_data_entityUpdate_copyEntityHistory",
                update_grid((data.entity_count + 63U) / 64U), values);
}

std::uint64_t upload_entity_voxels(WorldCompute& compute, const world::model::EntityGpuData& data,
                                   std::size_t previous_words) {
    // Entity types are immutable and append-only in the original handler. A same-size
    // replacement or shrink still receives a full upload for caller correctness.
    const auto first = data.voxels.size() > previous_words ? previous_words : 0;
    const auto words = std::span(data.voxels).subspan(first);
    const auto bytes = std::as_bytes(words);
    compute.upload("entityVoxelData", bytes, first * sizeof(std::uint32_t));
    return bytes.size();
}

} // namespace

void WorldUpdates::entities(const FrameInput& input, unsigned history_index) {
    require_ready();
    uploaded_entity_bytes_ = 0;
    if (!input.entities) {
        return;
    }
    const auto& data = *input.entities;
    update_entity_chunks(compute_, data, chunks_);
    uploaded_entity_bytes_ += data.changed_chunks.size() * sizeof(GroupRecord);
    if (entity_revision_ != data.revision) {
        copy_entity_instances(compute_, data);
        uploaded_entity_bytes_ += data.instances.size() * sizeof(data.instances.front());
        entity_revision_ = data.revision;
    }
    if (entity_voxel_revision_ != data.voxel_revision) {
        uploaded_entity_bytes_ += upload_entity_voxels(compute_, data, entity_voxel_words_);
        entity_voxel_words_ = data.voxels.size();
        entity_voxel_revision_ = data.voxel_revision;
    }
    copy_entity_history(compute_, data, history_index);
    uploaded_entity_bytes_ += data.entity_count * sizeof(std::array<std::uint32_t, 4>);
}

std::uint64_t WorldUpdates::uploaded_entity_bytes() const {
    return uploaded_entity_bytes_;
}

} // namespace engine::rendering::vulkan
