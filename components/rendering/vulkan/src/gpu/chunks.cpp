#include "device.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {

constexpr auto kChunkUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                             vk::ImageUsageFlagBits::eTransferSrc |
                             vk::ImageUsageFlagBits::eTransferDst;
constexpr auto kPackedFlags =
    vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage;
constexpr std::array kPackedFormats{vk::Format::eR32G32Uint, vk::Format::eR64Uint};

vk::Result packed_image_properties(vk::PhysicalDevice physical,
                                   vk::ImageFormatProperties2& properties) {
    const vk::ImageFormatListCreateInfo formats(kPackedFormats);
    const vk::PhysicalDeviceImageFormatInfo2 info(vk::Format::eR32G32Uint, vk::ImageType::e3D,
                                                  vk::ImageTiling::eOptimal, kChunkUsage,
                                                  kPackedFlags, &formats);
    return physical.getImageFormatProperties2(&info, &properties);
}

bool supports_packed_formats(vk::PhysicalDevice physical) {
    const auto required =
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eStorageImage |
        vk::FormatFeatureFlagBits::eTransferSrc | vk::FormatFeatureFlagBits::eTransferDst;
    const auto words = physical.getFormatProperties(vk::Format::eR32G32Uint);
    const auto atomic = physical.getFormatProperties(vk::Format::eR64Uint);
    const auto atomic_required =
        vk::FormatFeatureFlagBits::eStorageImage | vk::FormatFeatureFlagBits::eStorageImageAtomic;
    if ((words.optimalTilingFeatures & required) != required ||
        (atomic.optimalTilingFeatures & atomic_required) != atomic_required) {
        return false;
    }
    vk::ImageFormatProperties2 properties;
    return packed_image_properties(physical, properties) == vk::Result::eSuccess;
}

bool supports_packed_chunks(vk::PhysicalDevice physical) {
    const auto extensions = physical.enumerateDeviceExtensionProperties();
    const bool found = std::ranges::any_of(extensions, [](const auto& extension) {
        return std::strcmp(extension.extensionName.data(),
                           VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME) == 0;
    });
    if (!found) {
        return false;
    }
    const auto features =
        physical.getFeatures2<vk::PhysicalDeviceFeatures2,
                              vk::PhysicalDeviceShaderImageAtomicInt64FeaturesEXT>();
    return features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64 &&
           features.get<vk::PhysicalDeviceShaderImageAtomicInt64FeaturesEXT>()
               .shaderImageInt64Atomics &&
           supports_packed_formats(physical);
}

std::shared_ptr<Image> make_packed_chunks(Device& device, vk::Extent3D extent) {
    vk::ImageFormatProperties2 properties;
    const auto result = packed_image_properties(device.physical, properties);
    const auto& limits = properties.imageFormatProperties;
    if (result != vk::Result::eSuccess || !extent.width || !extent.height || !extent.depth ||
        extent.width > limits.maxExtent.width || extent.height > limits.maxExtent.height ||
        extent.depth > limits.maxExtent.depth ||
        std::uint64_t(extent.width) * extent.height > limits.maxResourceSize / 8 / extent.depth) {
        throw std::runtime_error("Packed chunk volume exceeds the RG32/R64 image format limits");
    }
    auto image = make_image(
        device, {extent, vk::Format::eR32G32Uint, kChunkUsage, true, kPackedFlags, kPackedFormats});
    const vk::ImageViewUsageCreateInfo usage(vk::ImageUsageFlagBits::eStorage);
    const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    const vk::ImageViewCreateInfo view({}, *image->handle, vk::ImageViewType::e3D,
                                       vk::Format::eR64Uint, {}, range, &usage);
    image->atomic_view = device.logical->createImageViewUnique(view);
    return image;
}

} // namespace

bool ChunkImages::packed() const {
    return geometry && geometry->format == vk::Format::eR32G32Uint;
}

bool ChunkImages::ready() const {
    return geometry && (packed() || entities);
}

std::uint64_t ChunkImages::allocation() const {
    return (geometry ? geometry->allocation : 0) + (entities ? entities->allocation : 0);
}

bool select_chunk_layout(vk::PhysicalDevice physical, ChunkLayout requested) {
    if (requested == ChunkLayout::split) {
        return false;
    }
    // RG32 stores R before G in memory. The R64 helper places R in the low word;
    // only select that representation where the native byte order agrees.
    if constexpr (std::endian::native != std::endian::little) {
        if (requested == ChunkLayout::packed) {
            throw std::runtime_error("Packed chunks require little-endian word order");
        }
        return false;
    }
    const bool supported = supports_packed_chunks(physical);
    if (!supported && requested == ChunkLayout::packed) {
        throw std::runtime_error("Packed chunks require shaderInt64, "
                                 "VK_EXT_shader_image_atomic_int64 and compatible RG32/R64 "
                                 "3D image formats");
    }
    return supported;
}

ChunkImages make_chunk_images(Device& device, vk::Extent3D extent) {
    if (device.packed_chunks) {
        return {make_packed_chunks(device, extent), nullptr};
    }
    const ImageDescription description{extent, vk::Format::eR32Uint, kChunkUsage, true};
    return {make_image(device, description), make_image(device, description)};
}

std::shared_ptr<Image> find_chunk_image(const ChunkImages& images, std::string_view name) {
    if (name == "chunks" || name == "chunkDataTex3D" || name == "chunkAtomic") {
        return images.geometry;
    }
    return name == "chunkEntities" ? images.entities : nullptr;
}

vk::ImageView chunk_image_view(const Image& image, std::string_view name) {
    if (name != "chunkAtomic") {
        return *image.view;
    }
    if (!image.atomic_view) {
        throw std::runtime_error("Atomic chunk view requires the packed image layout");
    }
    return *image.atomic_view;
}

} // namespace engine::rendering::vulkan
