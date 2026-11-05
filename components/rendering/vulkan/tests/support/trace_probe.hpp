#pragma once

#include "../../src/gpu/device.hpp"
#include "../../src/program/reflection.hpp"

#include <engine/world/model/world.hpp>

#include <array>
#include <map>
#include <string>

namespace engine::rendering::vulkan::test {

using Ray = std::array<float, 8>;
using RayRecord = std::array<std::uint32_t, 8>;

struct TraceProbe {
    platform::window::Window window;
    Device device;
    Reflection reflection;
    vk::UniqueDescriptorSetLayout descriptor_layout;
    vk::UniqueDescriptorPool descriptor_pool;
    vk::DescriptorSet descriptors;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniqueShaderModule module;
    vk::UniquePipeline pipeline;
    vk::UniqueCommandPool command_pool;
    vk::CommandBuffer command;
    std::map<std::string, std::shared_ptr<Buffer>, std::less<>> buffers;
    ChunkImages chunks;

    TraceProbe(const std::filesystem::path& binary, const std::filesystem::path& reflected,
               ChunkLayout layout = ChunkLayout::automatic);
    [[nodiscard]] std::vector<RayRecord> run(const world::model::World& world,
                                             std::span<const Ray> rays);
    void create_pipeline(const std::filesystem::path& binary);
    void create_descriptors();
    void write_descriptors();
    void prepare_buffers(const world::model::World& world, std::span<const Ray> rays);
    void upload_chunks(const world::model::World& world);
    void write_uniform(std::string_view name, std::span<const std::byte> data);
    void store(std::string name, std::span<const std::byte> bytes);
};

} // namespace engine::rendering::vulkan::test
