#ifndef ENGINE_RENDERING_VULKAN_SRC_WORLD_UPDATES_GROUPS_HPP
#define ENGINE_RENDERING_VULKAN_SRC_WORLD_UPDATES_GROUPS_HPP

#include <engine/world/model/types.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace engine::rendering::vulkan {

using GroupRecord = std::array<std::uint32_t, 2>;

[[nodiscard]] world::model::Vec3i aadf_group_dimensions(world::model::Vec3i chunks);

// Original ChangeHandler's CPU frontier; the resulting limits are applied by
// worldChange, and all persistent AADF growth remains GPU work.
class GroupChanges {
  public:
    void initialize(world::model::Vec3i chunks);
    [[nodiscard]] world::model::Vec3i dimensions() const;
    [[nodiscard]] std::uint32_t count() const;
    [[nodiscard]] std::span<const GroupRecord> build(std::span<const std::uint32_t> chunks);

  private:
    world::model::Vec3i chunks_;
    world::model::Vec3i groups_;
    std::vector<std::uint32_t> distances_;
    std::vector<std::uint32_t> changed_;
    std::deque<std::uint32_t> pending_;
    std::vector<GroupRecord> records_;
    void mark(std::uint32_t chunk);
    void expand(std::uint32_t group);
    void refine(std::size_t original_count);
    void refine_axis(std::size_t original_count, unsigned axis);
    void emit(std::size_t original_count);
};

} // namespace engine::rendering::vulkan

#endif
