#include "../world/updates.hpp"

#include "support.hpp"

namespace engine::rendering::vulkan {

WorldUpdates::WorldUpdates(WorldCompute& compute) : compute_(compute) {}

void WorldUpdates::initialize(world::model::Vec3i chunks) {
    validate_aadf_capacity(compute_.device, chunks);
    ready_ = false;
    chunks_ = chunks;
    changes_.initialize(chunks);
    frame_ = 0x7FFFFFFFU;
    entity_revision_ = ~std::uint64_t{0};
    entity_voxel_revision_ = ~std::uint64_t{0};
    uploaded_entity_bytes_ = 0;
    entity_voxel_words_ = 0;
    const vk::DeviceSize groups = changes_.count();
    std::array<GroupRecord, 96> queues{};
    std::fill_n(queues.begin(), 3, GroupRecord{0, changes_.count()});
    const std::array<std::uint32_t, 5> dispatch{1, 1, 1, 0, 0};
    compute_.allocate("boundQueueInfo", sizeof(queues), false);
    compute_.upload("boundQueueInfo", std::as_bytes(std::span(queues)));
    compute_.allocate("boundRefinedInfo", 3 * sizeof(std::uint32_t));
    compute_.allocate("boundGroupQueues", 96 * groups * sizeof(std::uint32_t), false);
    compute_.allocate("boundGroupMasks", groups * 3 * sizeof(std::uint32_t), false);
    compute_.allocate("boundGroupQueueDispatchCount", sizeof(dispatch), false);
    compute_.upload("boundGroupQueueDispatchCount", std::as_bytes(std::span(dispatch)));
    compute_.allocate("entityInstancesHistory", 16384U * 128U * 16U);
    compute_.allocate("entityChunkInstances", 20);
    compute_.allocate("entityVoxelData", 4);
    auto values = update_values(chunks_, changes_);
    set_value(values, "boundsInitOffset", 0U);
    compute_.run("world_data_boundsCalc_addInitialGroupsToBoundQueue",
                 update_grid((changes_.count() + 63U) / 64U), values);
    ready_ = true;
}

void WorldUpdates::require_ready() const {
    if (!ready_) {
        throw std::logic_error("Original world update queues have not been initialized");
    }
}

void WorldUpdates::bounds(unsigned budget) {
    require_ready();
    if (budget == 0) {
        return;
    }
    auto values = update_values(chunks_, changes_);
    set_value(values, "frameIndex", --frame_);
    set_value(values, "maxGroupBoundDispatch", budget);
    for (unsigned iteration = 0; iteration < 5; ++iteration) {
        compute_.run("world_data_boundsCalc_prepareGroupBounds", {1, 1, 1}, values);
        compute_.run_indirect("world_data_boundsCalc_computeGroupBounds",
                              "boundGroupQueueDispatchCount", values);
    }
}

} // namespace engine::rendering::vulkan
