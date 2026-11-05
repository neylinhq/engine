#include "pattern.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>

namespace engine::rendering::vulkan::test {
namespace {

constexpr std::uint64_t kHashInitial = 14695981039346656037ULL;
constexpr std::uint64_t kHashMultiplier = 1099511628211ULL;
constexpr std::uint64_t kMaximumTileBytes = 16U * 1024U * 1024U;
constexpr auto kStagingName = "chunk_pattern_staging";

struct ChunkSlab {
    vk::Extent3D extent;
    std::uint32_t first_z = 0;
    std::uint32_t depth = 0;
    bool packed = false;
};

std::uint64_t chunk_count(vk::Extent3D extent) {
    return std::uint64_t(extent.width) * extent.height * extent.depth;
}

std::uint32_t geometry_word(std::uint64_t index) {
    const auto state = static_cast<std::uint32_t>(index) * 747796405U + 2891336453U;
    const auto word = ((state >> ((state >> 28U) + 4U)) ^ state) * 277803737U;
    auto bounds = ((word >> 22U) ^ word) & 0x3FFFFFFFU;
    for (unsigned shift = 0; shift < 30; shift += 5) {
        if (((bounds >> shift) & 31U) == 0) {
            bounds |= 1U << shift;
        }
    }
    return bounds;
}

std::uint64_t uniform_fingerprint(std::uint64_t count) {
    auto result = kHashInitial;
    auto multiplier = kHashMultiplier;
    std::uint64_t addend = 0x3FFFFFFFU;
    while (count != 0) {
        if ((count & 1U) != 0) {
            result = result * multiplier + addend;
        }
        addend = addend * multiplier + addend;
        multiplier *= multiplier;
        count >>= 1U;
    }
    return result;
}

void clear_image(WorldCompute& compute, const Image& image, std::uint32_t geometry) {
    const auto command = compute.command();
    image_barrier(command, *image.handle, vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eTransferDstOptimal);
    const vk::ClearColorValue color(std::array<std::uint32_t, 4>{geometry, 0, 0, 0});
    const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    command.clearColorImage(*image.handle, vk::ImageLayout::eTransferDstOptimal, color, range);
    image_barrier(command, *image.handle, vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageLayout::eGeneral);
}

ChunkSlab make_slab(const ChunkImages& chunks) {
    const auto extent = chunks.geometry->extent;
    const auto plane_bytes =
        std::uint64_t(extent.width) * extent.height * (chunks.packed() ? 8U : 4U);
    if (plane_bytes == 0 || plane_bytes > kMaximumTileBytes) {
        throw std::runtime_error("Chunk pattern plane exceeds the bounded staging tile");
    }
    const auto depth =
        std::min(extent.depth, static_cast<std::uint32_t>(kMaximumTileBytes / plane_bytes));
    return {extent, 0, depth, chunks.packed()};
}

std::uint64_t fill_slab(Buffer& staging, const ChunkSlab& slab, std::uint64_t hash) {
    const auto plane = std::uint64_t(slab.extent.width) * slab.extent.height;
    const auto begin = plane * slab.first_z;
    const auto count = static_cast<std::size_t>(plane * slab.depth);
    const auto stride = slab.packed ? 2U : 1U;
    const std::span words(static_cast<std::uint32_t*>(staging.mapped), count * stride);
    for (std::size_t index = 0; index < count; ++index) {
        const auto geometry = geometry_word(begin + index);
        words[index * stride] = geometry;
        if (slab.packed) {
            words[index * stride + 1] = 0;
        }
        hash = hash * kHashMultiplier + geometry;
    }
    return hash;
}

void upload_slab(WorldCompute& compute, const Buffer& staging, const ChunkSlab& slab) {
    vk::BufferImageCopy copy;
    copy.setImageSubresource(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
        .setImageOffset(vk::Offset3D(0, 0, static_cast<std::int32_t>(slab.first_z)))
        .setImageExtent(vk::Extent3D(slab.extent.width, slab.extent.height, slab.depth));
    compute.command().copyBufferToImage(*staging.handle, *compute.chunks.geometry->handle,
                                        vk::ImageLayout::eTransferDstOptimal, copy);
    // The mapped transfer buffer may be reused only after this copy completes.
    compute.synchronize();
}

std::uint64_t sample_index(std::uint64_t chunks, std::uint32_t sample, std::uint32_t samples) {
    return samples == 1 ? 0 : (chunks - 1) * sample / (samples - 1);
}

vk::BufferImageCopy sample_copy(vk::Extent3D extent, std::uint64_t index, vk::DeviceSize offset) {
    const auto x = static_cast<std::int32_t>(index % extent.width);
    const auto y = static_cast<std::int32_t>((index / extent.width) % extent.height);
    const auto z = static_cast<std::int32_t>(index / (std::uint64_t(extent.width) * extent.height));
    vk::BufferImageCopy copy;
    copy.setBufferOffset(offset)
        .setImageSubresource(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
        .setImageOffset(vk::Offset3D(x, y, z))
        .setImageExtent(vk::Extent3D(1, 1, 1));
    return copy;
}

void record_samples(WorldCompute& compute, const Buffer& staging, std::uint32_t samples) {
    const auto& chunks = compute.chunks;
    const auto& geometry = *chunks.geometry;
    const auto command = compute.command();
    image_barrier(command, *geometry.handle, vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageLayout::eTransferSrcOptimal);
    if (chunks.entities) {
        image_barrier(command, *chunks.entities->handle, vk::ImageLayout::eGeneral,
                      vk::ImageLayout::eTransferSrcOptimal);
    }
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        const auto index = sample_index(chunk_count(geometry.extent), sample, samples);
        auto copy = sample_copy(geometry.extent, index, sample * 8U);
        command.copyImageToBuffer(*geometry.handle, vk::ImageLayout::eTransferSrcOptimal,
                                  *staging.handle, copy);
        if (chunks.entities) {
            copy.setBufferOffset(sample * 8U + 4U);
            command.copyImageToBuffer(*chunks.entities->handle,
                                      vk::ImageLayout::eTransferSrcOptimal, *staging.handle, copy);
        }
    }
    for (const auto& image : {chunks.geometry, chunks.entities}) {
        if (image) {
            image_barrier(command, *image->handle, vk::ImageLayout::eTransferSrcOptimal,
                          vk::ImageLayout::eGeneral);
        }
    }
    const vk::MemoryBarrier2 readback(
        vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead);
    command.pipelineBarrier2(vk::DependencyInfo({}, readback));
    compute.synchronize();
}

void verify_samples(WorldCompute& compute, Buffer& staging, std::uint32_t samples) {
    record_samples(compute, staging, samples);
    const auto count = chunk_count(compute.chunks.geometry->extent);
    const std::span words(static_cast<const std::uint32_t*>(staging.mapped), samples * 2U);
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        const auto index = sample_index(count, sample, samples);
        if (words[sample * 2U] != geometry_word(index) || words[sample * 2U + 1] != 0) {
            throw std::runtime_error("Varied chunk image readback differs from its input pattern");
        }
    }
}

ChunkPatternInfo initialize_varied(WorldCompute& compute) {
    auto slab = make_slab(compute.chunks);
    const auto samples =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(chunk_count(slab.extent), 16));
    const auto plane = std::uint64_t(slab.extent.width) * slab.extent.height;
    const auto tile_bytes = plane * slab.depth * (slab.packed ? 8U : 4U);
    const auto staging_bytes = std::max(tile_bytes, std::uint64_t(samples) * 8U);
    const auto usage =
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
    const auto staging = make_buffer(compute.device, {staging_bytes, usage, true});
    // Keep it alive even if command recording or submission throws before completion.
    compute.buffers[kStagingName] = staging;
    image_barrier(compute.command(), *compute.chunks.geometry->handle, vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eTransferDstOptimal);
    if (compute.chunks.entities) {
        clear_image(compute, *compute.chunks.entities, 0);
    }
    auto hash = kHashInitial;
    const auto tile_depth = slab.depth;
    while (slab.first_z < slab.extent.depth) {
        slab.depth = std::min(tile_depth, slab.extent.depth - slab.first_z);
        hash = fill_slab(*staging, slab, hash);
        upload_slab(compute, *staging, slab);
        slab.first_z += slab.depth;
    }
    verify_samples(compute, *staging, samples);
    compute.buffers.erase(kStagingName);
    return {hash, staging->size, staging->allocation, samples};
}

} // namespace

ChunkPatternInfo initialize_chunk_pattern(WorldCompute& compute, ChunkPattern pattern) {
    if (pattern == ChunkPattern::varied) {
        return initialize_varied(compute);
    }
    clear_image(compute, *compute.chunks.geometry, 0x3FFFFFFFU);
    if (compute.chunks.entities) {
        clear_image(compute, *compute.chunks.entities, 0);
    }
    return {uniform_fingerprint(chunk_count(compute.chunks.geometry->extent)), 0, 0, 0};
}

} // namespace engine::rendering::vulkan::test
