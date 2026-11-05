#include "../frame/values.hpp"
#include "../world/compute.hpp"
#include "../world/generation.hpp"
#include "../world_updates/support.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace engine::rendering::vulkan {
bool uniform_source(const world::model::Model& source, world::model::IVec3 requested,
                    int segment_groups) {
    if (segment_groups < 1 || segment_groups > 8 || requested != source.size) {
        return false;
    }
    const auto side = segment_groups * 64;
    return source.size.x % side == 0 && source.size.y % side == 0 && source.size.z % side == 0 &&
           std::ranges::all_of(source.chunks, [](auto word) { return (word >> 30) <= 1; });
}

GenerationResult import_uniform(WorldCompute& compute, const world::model::Model& source) {
    world::model::validate(source);
    const auto size = world::model::chunk_size(source.size);
    validate_aadf_capacity(compute.device, size);
    const auto maximum = compute.device.properties.limits.maxImageDimension3D;
    const vk::Extent3D extent(static_cast<std::uint32_t>(size.x),
                              static_cast<std::uint32_t>(size.y),
                              static_cast<std::uint32_t>(size.z));
    if (extent.width > maximum || extent.height > maximum || extent.depth > maximum) {
        throw std::runtime_error("Uniform chunk volume exceeds GPU limits");
    }
    GenerationResult result;
    result.model = source;
    result.model.blocks.assign(64, 0);
    result.model.voxels.assign(32, 0);
    result.actual_size = source.size;
    result.logical_voxels = 64;
    result.block_words = 64;
    compute.synchronize();
    compute.allocate("blocks", 64 * sizeof(std::uint32_t));
    compute.allocate("voxels", 32 * sizeof(std::uint32_t));
    compute.chunks = make_chunk_images(compute.device, extent);
    const auto range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    for (const auto& image : {compute.chunks.geometry, compute.chunks.entities}) {
        if (image) {
            image_barrier(compute.command(), *image->handle, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal);
        }
    }
    if (compute.chunks.entities) {
        compute.command().clearColorImage(
            *compute.chunks.entities->handle, vk::ImageLayout::eTransferDstOptimal,
            vk::ClearColorValue(std::array<std::uint32_t, 4>{}), range);
    }
    const auto plane_words = static_cast<std::size_t>(size.x) * size.y;
    const auto chunk_bytes = compute.chunks.packed() ? 8U : 4U;
    const auto batch_depth =
        std::max<std::size_t>(1, (8U * 1024U * 1024U) / (plane_words * chunk_bytes));
    for (std::uint32_t z = 0; z < extent.depth;) {
        const auto depth =
            static_cast<std::uint32_t>(std::min(batch_depth, std::size_t(extent.depth - z)));
        const auto words =
            std::span(source.chunks)
                .subspan(std::size_t(z) * plane_words, std::size_t(depth) * plane_words);
        auto staging = make_buffer(compute.device, {words.size() * chunk_bytes,
                                                    vk::BufferUsageFlagBits::eTransferSrc, true});
        if (compute.chunks.packed()) {
            // RG32/R64 uses the geometry word first and zero entity membership second.
            auto* packed = static_cast<std::uint64_t*>(staging->mapped);
            std::transform(words.begin(), words.end(), packed,
                           [](std::uint32_t word) { return std::uint64_t{word}; });
        } else {
            std::memcpy(staging->mapped, words.data(), words.size_bytes());
        }
        vk::BufferImageCopy copy;
        copy.setImageSubresource(
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
            .setImageOffset(vk::Offset3D(0, 0, static_cast<std::int32_t>(z)))
            .setImageExtent(vk::Extent3D(extent.width, extent.height, depth));
        compute.command().copyBufferToImage(*staging->handle, *compute.chunks.geometry->handle,
                                            vk::ImageLayout::eTransferDstOptimal, copy);
        compute.synchronize(); // Staging remains alive until its transfer completes.
        z += depth;
    }
    for (const auto& image : {compute.chunks.geometry, compute.chunks.entities}) {
        if (image) {
            image_barrier(compute.command(), *image->handle, vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eGeneral);
        }
    }
    UniformValues values;
    set_value(values, "boundsGroupOffset", 0U);
    compute.run("world_data_chunkCalc_computeVoxelBounds", {1, 1, 1}, values);
    compute.run("world_data_chunkCalc_computeBlockBounds", {1, 1, 1}, values);
    compute.synchronize();
    std::cout << "Imported " << source.chunks.size() << " uniform GPU chunks\n" << std::flush;
    return result;
}
} // namespace engine::rendering::vulkan
