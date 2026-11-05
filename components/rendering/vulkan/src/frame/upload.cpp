#include "../state.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
vk::Extent3D chunk_extent(const FrameInput& input, std::uint32_t maximum_dimension) {
    const auto size = world::model::chunk_size(input.world.size);
    const vk::Extent3D extent(static_cast<std::uint32_t>(size.x),
                              static_cast<std::uint32_t>(size.y),
                              static_cast<std::uint32_t>(size.z));
    if (!extent.width || !extent.height || !extent.depth || extent.width > maximum_dimension ||
        extent.height > maximum_dimension || extent.depth > maximum_dimension) {
        throw std::runtime_error("World chunk texture exceeds GPU volume dimensions");
    }
    return extent;
}

std::vector<std::uint32_t> chunk_words(const FrameInput& input, bool packed) {
    const auto count = input.world.chunks.size();
    std::vector<std::uint32_t> words(count * 2);
    if (packed) {
        for (std::size_t index = 0; index < count; ++index) {
            words[index * 2] = input.world.chunks[index];
            if (input.entities && index < input.entities->chunks.size()) {
                words[index * 2 + 1] = input.entities->chunks[index];
            }
        }
        return words;
    }
    std::ranges::copy(input.world.chunks, words.begin());
    if (input.entities) {
        std::copy_n(input.entities->chunks.begin(), std::min(count, input.entities->chunks.size()),
                    words.begin() + count);
    }
    return words;
}
} // namespace

void Renderer::Impl::copy_to_buffer(Buffer& target, std::span<const std::byte> data,
                                    vk::DeviceSize offset) {
    if (data.empty()) {
        return;
    }
    if (offset + data.size() > target.size) {
        throw std::runtime_error("World upload exceeds its GPU allocation");
    }
    auto staging = make_buffer(device, {data.size(), vk::BufferUsageFlagBits::eTransferSrc, true});
    std::memcpy(staging->mapped, data.data(), data.size());
    uploads.push_back(staging);
    command.copyBuffer(*staging->handle, *target.handle, vk::BufferCopy(0, offset, data.size()));
}

void Renderer::Impl::upload_buffer(std::string_view name, std::span<const std::byte> data) {
    const auto bytes = std::max<vk::DeviceSize>(data.size(), 4);
    if (bytes > device.properties.limits.maxStorageBufferRange) {
        throw std::runtime_error("World buffer exceeds GPU storage range: " + std::string(name));
    }
    auto& target = buffers[std::string(name)];
    if (!target || target->size != bytes) {
        const auto flags =
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        target = make_buffer(device, {bytes, flags, false});
        if (data.empty()) {
            command.fillBuffer(*target->handle, 0, target->size, 0);
        }
    }
    copy_to_buffer(*target, data);
}

void Renderer::Impl::upload_chunks(const FrameInput& input) {
    const auto extent = chunk_extent(input, device.properties.limits.maxImageDimension3D);
    if (!chunks.ready() || chunks.geometry->extent != extent ||
        (chunks.entities && chunks.entities->extent != extent)) {
        chunks = make_chunk_images(device, extent);
        chunks_ready = false;
    }
    const auto words = chunk_words(input, chunks.packed());
    const auto bytes = std::as_bytes(std::span(words));
    if (input.entities) {
        status.entity_upload_bytes += input.world.chunks.size_bytes();
    }
    auto staging = make_buffer(device, {bytes.size(), vk::BufferUsageFlagBits::eTransferSrc, true});
    std::memcpy(staging->mapped, bytes.data(), bytes.size());
    uploads.push_back(staging);
    upload_chunk_image(*chunks.geometry, *staging, 0);
    if (chunks.entities) {
        upload_chunk_image(*chunks.entities, *staging, input.world.chunks.size_bytes());
    }
    chunks_ready = true;
}

void Renderer::Impl::upload_chunk_image(const Image& image, const Buffer& staging,
                                        vk::DeviceSize offset) {
    image_barrier(command, *image.handle,
                  chunks_ready ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eTransferDstOptimal);
    vk::BufferImageCopy copy;
    copy.setBufferOffset(offset)
        .setImageSubresource(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
        .setImageExtent(image.extent);
    command.copyBufferToImage(*staging.handle, *image.handle, vk::ImageLayout::eTransferDstOptimal,
                              copy);
    image_barrier(command, *image.handle, vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageLayout::eGeneral);
}

void Renderer::Impl::upload_entities(const FrameInput& input) {
    const auto flags =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
    auto& history_buffer = buffers["entityInstancesHistory"];
    if (!history_buffer) {
        history_buffer = make_buffer(device, {16384u * 128u * 16u, flags, false});
        command.fillBuffer(*history_buffer->handle, 0, history_buffer->size, 0);
        memory_barrier(command);
    }
    if (!input.entities) {
        upload_buffer("entityChunkInstances", {});
        upload_buffer("entityVoxelData", {});
        entity_revision = ~std::uint64_t{0};
        entity_voxel_revision = ~std::uint64_t{0};
        return;
    }
    if (entity_revision != input.entities->revision) {
        upload_buffer("entityChunkInstances", std::as_bytes(std::span(input.entities->instances)));
        status.entity_upload_bytes += input.entities->instances.size() * 20u;
        entity_revision = input.entities->revision;
    }
    if (entity_voxel_revision != input.entities->voxel_revision) {
        upload_buffer("entityVoxelData", std::as_bytes(std::span(input.entities->voxels)));
        status.entity_upload_bytes += input.entities->voxels.size() * 4u;
        entity_voxel_revision = input.entities->voxel_revision;
    }
    if (input.entities->instances.empty()) {
        return;
    }
    const auto index = 127u - static_cast<unsigned>(serial % 128u);
    const auto first = static_cast<std::size_t>(index) * 16384u;
    if (input.entities->history.size() >= first + 16384u) {
        const auto current = std::span(input.entities->history).subspan(first, 16384);
        copy_to_buffer(*history_buffer, std::as_bytes(current), first * 16u);
        status.entity_upload_bytes += current.size() * 16u;
    }
}

void Renderer::Impl::upload_world(const FrameInput& input) {
    if (input.world.gpu_authoritative) {
        update_gpu_world(input);
        return;
    }
    if (input.world.geometry_revision != geometry_revision) {
        upload_buffer("blocks", std::as_bytes(input.world.blocks));
        upload_buffer("voxels", std::as_bytes(input.world.voxels));
        geometry_revision = input.world.geometry_revision;
    }
    if (input.world.material_revision != material_revision) {
        upload_buffer("voxelTypeData", std::as_bytes(input.world.materials));
        material_revision = input.world.material_revision;
    }
    if (input.world.chunk_revision != chunk_revision || !chunks.ready()) {
        upload_chunks(input);
        chunk_revision = input.world.chunk_revision;
    }
    upload_entities(input);
    world_revision = input.world.revision;
}

} // namespace engine::rendering::vulkan
