#pragma once

#include "../../src/world/compute.hpp"
#include "../../src/world/updates.hpp"

#include <engine/world/model/world.hpp>

#include <string_view>

namespace engine::rendering::vulkan::test {

void require_update(bool condition, std::string_view message);
[[nodiscard]] std::vector<std::uint32_t> update_words(WorldCompute& compute,
                                                      const std::string& name, std::size_t count);
[[nodiscard]] std::vector<GroupRecord> update_chunks(WorldCompute& compute);
[[nodiscard]] world::model::Model update_mirror(const world::model::World& world);

struct UpdateProbe {
    platform::window::Window window;
    Device device;
    BufferMap buffers;
    ChunkImages chunks;
    WorldCompute compute;
    WorldUpdates updates;
    world::model::World world;

    explicit UpdateProbe(const std::filesystem::path& shaders,
                         ChunkLayout layout = ChunkLayout::automatic);
    void load(world::model::Model source, world::model::IVec3 size);
    void flush(unsigned history_index, unsigned budget = 32768);
    void settle(unsigned budget = 32768, unsigned frames = 20);
    [[nodiscard]] world::model::Model snapshot();
};

} // namespace engine::rendering::vulkan::test
