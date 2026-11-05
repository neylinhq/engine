#pragma once

#include "../world_updates/groups.hpp"

#include <engine/rendering/vulkan/renderer.hpp>

#include <cstddef>

namespace engine::rendering::vulkan {

class WorldCompute;

class WorldUpdates {
  public:
    explicit WorldUpdates(WorldCompute& compute);
    void initialize(world::model::IVec3 chunks);
    void apply(const FrameInput& input);
    void entities(const FrameInput& input, unsigned history_index);
    void bounds(unsigned budget);
    [[nodiscard]] std::uint64_t uploaded_entity_bytes() const;

  private:
    WorldCompute& compute_;
    GroupChanges changes_;
    world::model::IVec3 chunks_;
    std::uint32_t frame_ = 0x7FFFFFFFU;
    std::uint64_t entity_revision_ = ~std::uint64_t{0};
    std::uint64_t entity_voxel_revision_ = ~std::uint64_t{0};
    std::uint64_t uploaded_entity_bytes_ = 0;
    std::size_t entity_voxel_words_ = 0;
    bool ready_ = false;
    void require_ready() const;
};

} // namespace engine::rendering::vulkan
