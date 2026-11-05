#include "../world/updates.hpp"

#include "support.hpp"

namespace engine::rendering::vulkan {
namespace {

template <std::size_t width>
void scatter_payload(WorldCompute& compute, const world::model::WorldView& view,
                     UniformValues values) {
    const auto offsets = width == 64 ? view.changed_blocks : view.changed_voxels;
    if (offsets.empty()) {
        return;
    }
    const auto words = width == 64 ? view.blocks : view.voxels;
    const auto records = payload_records<width>(offsets, words);
    const auto buffer = width == 64 ? "changedBlocksDynamic" : "changedVoxelsDynamic";
    const auto field = width == 64 ? "changedBlockCount" : "changedVoxelCount";
    const auto program = width == 64 ? "world_data_worldChange_applyBlockChange"
                                     : "world_data_worldChange_applyVoxelChange";
    compute.grow(width == 64 ? "blocks" : "voxels", words.size_bytes());
    compute.upload(buffer, std::as_bytes(std::span(records)));
    set_value(values, field, update_count(records.size()));
    compute.run(program, update_grid(update_count(records.size())), values);
}

} // namespace

void WorldUpdates::apply(const FrameInput& input) {
    require_ready();
    auto values = update_values(chunks_, changes_);
    if (!input.world.changed_chunks.empty()) {
        const auto records = chunk_records(input.world.changed_chunks, input.world.chunks, chunks_);
        compute_.upload("changedChunksDynamic", std::as_bytes(std::span(records)));
        set_value(values, "changedChunkCount", update_count(records.size()));
        compute_.run("world_data_worldChange_applyChunkChange",
                     update_grid((update_count(records.size()) + 63U) / 64U), values);
    }
    scatter_payload<64>(compute_, input.world, values);
    scatter_payload<32>(compute_, input.world, values);
    if (!input.world.invalidated_chunks.empty()) {
        const auto records = changes_.build(input.world.invalidated_chunks);
        compute_.upload("changedGroupsDynamic", std::as_bytes(records));
        set_value(values, "changedGroupCount", update_count(records.size()));
        compute_.run("world_data_worldChange_applyGroupChange",
                     update_grid(update_count(records.size())), values);
    }
}

} // namespace engine::rendering::vulkan
