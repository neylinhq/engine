#include "../state.hpp"

#include <stdexcept>

namespace engine::rendering::vulkan {

void Renderer::Impl::copy_scene() {
    if (!scene_output) {
        scene_output = make_image(device, {{surface_extent.width, surface_extent.height, 1},
                                           color_format,
                                           vk::ImageUsageFlagBits::eTransferSrc |
                                               vk::ImageUsageFlagBits::eTransferDst |
                                               vk::ImageUsageFlagBits::eColorAttachment,
                                           false});
    }
    image_barrier(command, *output->handle, vk::ImageLayout::eColorAttachmentOptimal,
                  vk::ImageLayout::eTransferSrcOptimal);
    image_barrier(command, *scene_output->handle, vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eTransferDstOptimal);
    const vk::ImageSubresourceLayers layer(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    const vk::ImageCopy copy(layer, {}, layer, {},
                             vk::Extent3D(surface_extent.width, surface_extent.height, 1));
    command.copyImage(*output->handle, vk::ImageLayout::eTransferSrcOptimal, *scene_output->handle,
                      vk::ImageLayout::eTransferDstOptimal, copy);
    image_barrier(command, *scene_output->handle, vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageLayout::eTransferSrcOptimal);
    image_barrier(command, *output->handle, vk::ImageLayout::eTransferSrcOptimal,
                  vk::ImageLayout::eColorAttachmentOptimal);
    scene_ready = true;
}

void Renderer::Impl::capture(const std::filesystem::path& path) {
    if (!output_ready || acquired) {
        throw std::runtime_error(
            "Screenshot requires capture_scene or capture_output on the rendered frame");
    }
    complete_frame();
    const auto bytes = vk::DeviceSize(surface_extent.width) * surface_extent.height * 4;
    const auto staging = make_buffer(device, {bytes, vk::BufferUsageFlagBits::eTransferDst, true});
    uploads.push_back(staging);
    command.reset();
    command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    vk::BufferImageCopy copy;
    copy.setImageSubresource(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
        .setImageExtent(vk::Extent3D(surface_extent.width, surface_extent.height, 1));
    const auto& source = scene_ready ? scene_output : output;
    command.copyImageToBuffer(*source->handle, vk::ImageLayout::eTransferSrcOptimal,
                              *staging->handle, copy);
    const vk::MemoryBarrier2 readback(
        vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead);
    command.pipelineBarrier2(vk::DependencyInfo({}, readback));
    command.end();
    device.logical->resetFences(*completion);
    const vk::CommandBufferSubmitInfo commands(command, 1);
    vk::SubmitInfo2 submit;
    submit.setCommandBufferInfos(commands);
    device.queue.submit2(submit, *completion);
    device.queue.waitIdle();
    const auto data =
        std::span(static_cast<const std::byte*>(staging->mapped), static_cast<std::size_t>(bytes));
    write_png(path, surface_extent, data, color_format == vk::Format::eB8G8R8A8Srgb);
}

} // namespace engine::rendering::vulkan
