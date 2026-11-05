#include "device.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace engine::rendering::vulkan {

Buffer::~Buffer() {
    if (mapped) {
        device.unmapMemory(*memory);
    }
}

std::shared_ptr<Buffer> make_buffer(Device& device, BufferDescription description) {
    auto result = std::make_shared<Buffer>();
    result->device = *device.logical;
    result->size = std::max<vk::DeviceSize>(description.size, 4);
    result->handle = device.logical->createBufferUnique(
        vk::BufferCreateInfo({}, result->size, description.usage));
    const auto required = device.logical->getBufferMemoryRequirements(*result->handle);
    const auto flags = description.host_visible ? vk::MemoryPropertyFlagBits::eHostVisible |
                                                      vk::MemoryPropertyFlagBits::eHostCoherent
                                                : vk::MemoryPropertyFlagBits::eDeviceLocal;
    result->memory = device.logical->allocateMemoryUnique(
        vk::MemoryAllocateInfo(required.size, device.memory_type(required.memoryTypeBits, flags)));
    result->allocation = required.size;
    device.logical->bindBufferMemory(*result->handle, *result->memory, 0);
    if (description.host_visible) {
        result->mapped = device.logical->mapMemory(*result->memory, 0, result->size);
    }
    return result;
}

std::shared_ptr<Image> make_image(Device& device, ImageDescription description) {
    auto result = std::make_shared<Image>();
    result->extent = description.extent;
    result->format = description.format;
    vk::ImageCreateInfo info;
    info.setFlags(description.flags)
        .setImageType(description.volume ? vk::ImageType::e3D : vk::ImageType::e2D)
        .setExtent(description.extent)
        .setFormat(description.format)
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(description.usage);
    const vk::ImageFormatListCreateInfo formats(description.view_formats);
    if (!description.view_formats.empty()) {
        info.setPNext(&formats);
    }
    result->handle = device.logical->createImageUnique(info);
    const auto required = device.logical->getImageMemoryRequirements(*result->handle);
    result->memory = device.logical->allocateMemoryUnique(vk::MemoryAllocateInfo(
        required.size,
        device.memory_type(required.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
    result->allocation = required.size;
    device.logical->bindImageMemory(*result->handle, *result->memory, 0);
    const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    const vk::ImageViewCreateInfo view(
        {}, *result->handle, description.volume ? vk::ImageViewType::e3D : vk::ImageViewType::e2D,
        description.format, {}, range);
    result->view = device.logical->createImageViewUnique(view);
    return result;
}

std::vector<std::uint32_t> read_program(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    if (!stream || size < 20 || size > 32 * 1024 * 1024 || size % 4 != 0) {
        throw std::runtime_error("Invalid or missing SPIR-V: " + path_text(path));
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(words.data()), size);
    if (!stream || words.front() != 0x07230203u) {
        throw std::runtime_error("SPIR-V read or magic failed: " + path_text(path));
    }
    return words;
}

void memory_barrier(vk::CommandBuffer command) {
    vk::MemoryBarrier2 barrier;
    barrier.setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands)
        .setSrcAccessMask(vk::AccessFlagBits2::eMemoryWrite)
        .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
        .setDstAccessMask(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
    vk::DependencyInfo dependency;
    dependency.setMemoryBarriers(barrier);
    command.pipelineBarrier2(dependency);
}

void image_barrier(vk::CommandBuffer command, vk::Image image, vk::ImageLayout before,
                   vk::ImageLayout after) {
    vk::ImageMemoryBarrier2 barrier;
    barrier.setOldLayout(before)
        .setNewLayout(after)
        .setImage(image)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands)
        .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
        .setSrcAccessMask(before == vk::ImageLayout::eUndefined
                              ? vk::AccessFlags2{}
                              : vk::AccessFlagBits2::eMemoryRead |
                                    vk::AccessFlagBits2::eMemoryWrite)
        .setDstAccessMask(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
        .setSubresourceRange(
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
    vk::DependencyInfo dependency;
    dependency.setImageMemoryBarriers(barrier);
    command.pipelineBarrier2(dependency);
}

} // namespace engine::rendering::vulkan
