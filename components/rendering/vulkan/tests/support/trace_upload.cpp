#include "trace_probe.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan::test {
namespace {

void upload_chunk_plane(TraceProbe& probe, const char* name, const Image& image,
                        std::span<const std::uint32_t> words) {
    probe.store(name, std::as_bytes(words));
    image_barrier(probe.command, *image.handle, vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eTransferDstOptimal);
    vk::BufferImageCopy copy;
    copy.setImageSubresource(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
        .setImageExtent(image.extent);
    probe.command.copyBufferToImage(*probe.buffers.at(name)->handle, *image.handle,
                                    vk::ImageLayout::eTransferDstOptimal, copy);
    image_barrier(probe.command, *image.handle, vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageLayout::eGeneral);
}

} // namespace

void TraceProbe::store(std::string name, std::span<const std::byte> bytes) {
    const auto usage = name == "uniform" ? vk::BufferUsageFlagBits::eUniformBuffer
                                         : vk::BufferUsageFlagBits::eStorageBuffer;
    auto buffer = make_buffer(device, {std::max<std::size_t>(4, bytes.size()),
                                       usage | vk::BufferUsageFlagBits::eTransferSrc, true});
    std::memset(buffer->mapped, 0, static_cast<std::size_t>(buffer->size));
    if (!bytes.empty()) {
        std::memcpy(buffer->mapped, bytes.data(), bytes.size());
    }
    buffers[std::move(name)] = std::move(buffer);
}

void TraceProbe::write_uniform(std::string_view name, std::span<const std::byte> data) {
    const auto found = reflection.uniforms.find(name);
    if (found == reflection.uniforms.end() || found->second.size < data.size()) {
        throw std::runtime_error("Missing or incompatible ray probe uniform: " + std::string(name));
    }
    auto* output = static_cast<std::byte*>(buffers.at("uniform")->mapped);
    std::memcpy(output + found->second.offset, data.data(), data.size());
}

void TraceProbe::prepare_buffers(const world::model::World& world, std::span<const Ray> rays) {
    buffers.clear();
    const auto view = world.view();
    const auto& entities = world.entity_gpu();
    store("blocks", std::as_bytes(view.blocks));
    store("voxels", std::as_bytes(view.voxels));
    store("voxelTypeData", std::as_bytes(view.materials));
    store("entityChunkInstances", std::as_bytes(std::span(entities.instances)));
    store("entityVoxelData", std::as_bytes(std::span(entities.voxels)));
    store("entityInstancesHistory", std::as_bytes(std::span(entities.history)));
    store("test_rays", std::as_bytes(rays));
    const std::vector<RayRecord> output(rays.size());
    store("test_results", std::as_bytes(std::span(output)));
    const std::vector<std::byte> uniform(reflection.uniform_size);
    store("uniform", uniform);
    const std::array<float, 3> lower{0.1F, 0.1F, 0.1F};
    const std::array<float, 3> upper{view.size.x - 0.1F, view.size.y - 0.1F, view.size.z - 0.1F};
    write_uniform("boundingBoxMin", std::as_bytes(std::span(lower)));
    write_uniform("boundingBoxMax", std::as_bytes(std::span(upper)));
    const std::array count{static_cast<std::uint32_t>(rays.size())};
    write_uniform("ray_count", std::as_bytes(std::span(count)));
}

void TraceProbe::upload_chunks(const world::model::World& world) {
    const auto view = world.view();
    const auto& entities = world.entity_gpu();
    const auto size = world::model::chunk_size(view.size);
    const vk::Extent3D extent(static_cast<unsigned>(size.x), static_cast<unsigned>(size.y),
                              static_cast<unsigned>(size.z));
    chunks = make_chunk_images(device, extent);
    if (chunks.packed()) {
        std::vector<std::uint32_t> packed(view.chunks.size() * 2);
        for (std::size_t index = 0; index < view.chunks.size(); ++index) {
            packed[index * 2] = view.chunks[index];
            packed[index * 2 + 1] = entities.chunks[index];
        }
        upload_chunk_plane(*this, "chunk_upload", *chunks.geometry, packed);
        return;
    }
    upload_chunk_plane(*this, "chunk_upload", *chunks.geometry, view.chunks);
    upload_chunk_plane(*this, "entity_chunk_upload", *chunks.entities, entities.chunks);
}

} // namespace engine::rendering::vulkan::test
