#include "update_probe.hpp"

#include "../../src/world/generation.hpp"

#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan::test {
namespace {

platform::window::Window create_window() {
    auto result = platform::window::Window::create({32, 32, "GPU world updates", false});
    if (!result) {
        throw std::runtime_error(result.error());
    }
    return std::move(*result);
}

void copy_chunk_plane(vk::CommandBuffer command, const Image& image, const Buffer& staging,
                      vk::DeviceSize offset) {
    image_barrier(command, *image.handle, vk::ImageLayout::eGeneral,
                  vk::ImageLayout::eTransferSrcOptimal);
    vk::BufferImageCopy copy;
    copy.setBufferOffset(offset)
        .setImageSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1})
        .setImageExtent(image.extent);
    command.copyImageToBuffer(*image.handle, vk::ImageLayout::eTransferSrcOptimal, *staging.handle,
                              copy);
    image_barrier(command, *image.handle, vk::ImageLayout::eTransferSrcOptimal,
                  vk::ImageLayout::eGeneral);
}

} // namespace

void require_update(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::vector<std::uint32_t> update_words(WorldCompute& compute, const std::string& name,
                                        std::size_t count) {
    const auto bytes = compute.read(name, 0, count * sizeof(std::uint32_t));
    std::vector<std::uint32_t> result(count);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

std::vector<GroupRecord> update_chunks(WorldCompute& compute) {
    const auto& image = *compute.chunks.geometry;
    const auto count =
        static_cast<std::size_t>(image.extent.width) * image.extent.height * image.extent.depth;
    const auto bytes = count * sizeof(GroupRecord);
    const auto staging =
        make_buffer(compute.device, {bytes, vk::BufferUsageFlagBits::eTransferDst, true});
    const auto command = compute.command();
    copy_chunk_plane(command, image, *staging, 0);
    if (!compute.chunks.packed()) {
        copy_chunk_plane(command, *compute.chunks.entities, *staging,
                         count * sizeof(std::uint32_t));
    }
    const vk::MemoryBarrier2 barrier(
        vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead);
    command.pipelineBarrier2(vk::DependencyInfo({}, barrier));
    compute.synchronize();
    std::vector<GroupRecord> result(count);
    const auto* words = static_cast<const std::uint32_t*>(staging->mapped);
    for (std::size_t index = 0; index < count; ++index) {
        result[index] = compute.chunks.packed()
                            ? GroupRecord{words[index * 2], words[index * 2 + 1]}
                            : GroupRecord{words[index], words[count + index]};
    }
    return result;
}

world::model::Model update_mirror(const world::model::World& world) {
    const auto view = world.view();
    world::model::Model result;
    result.size = view.size;
    result.materials.assign(world.materials().begin(), world.materials().end());
    result.chunks.assign(view.chunks.begin(), view.chunks.end());
    result.blocks.assign(view.blocks.begin(), view.blocks.end());
    result.voxels.assign(view.voxels.begin(), view.voxels.end());
    return result;
}

UpdateProbe::UpdateProbe(const std::filesystem::path& shaders, ChunkLayout layout)
    : window(create_window()), device(window, true, layout),
      compute(device, shaders, buffers, chunks), updates(compute) {}

void UpdateProbe::load(world::model::Model source, world::model::IVec3 size) {
    source = world.prepare_generation(std::move(source));
    auto result = generate_world(compute, source, size, 1);
    updates.initialize(world::model::chunk_size(result.model.size));
    world.adopt_generated({std::move(result.model), result.actual_size, std::move(result.hash),
                           result.compression, result.logical_voxels, result.block_words});
    world.acknowledge_changes();
    compute.synchronize();
}

void UpdateProbe::flush(unsigned history_index, unsigned budget) {
    FrameInput input;
    input.world = world.view();
    input.entities = &world.entity_gpu();
    updates.entities(input, history_index);
    updates.apply(input);
    updates.bounds(budget);
    compute.synchronize();
    world.acknowledge_changes();
}

void UpdateProbe::settle(unsigned budget, unsigned frames) {
    for (unsigned frame = 0; frame < frames; ++frame) {
        updates.bounds(budget);
    }
    compute.synchronize();
}

world::model::Model UpdateProbe::snapshot() {
    auto result = update_mirror(world);
    const auto image = update_chunks(compute);
    for (std::size_t index = 0; index < image.size(); ++index) {
        result.chunks[index] = image[index][0];
    }
    result.blocks = update_words(compute, "blocks", result.blocks.size());
    result.voxels = update_words(compute, "voxels", result.voxels.size());
    return result;
}

} // namespace engine::rendering::vulkan::test
