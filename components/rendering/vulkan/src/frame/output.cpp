#include "../state.hpp"

namespace engine::rendering::vulkan {
namespace {

void swapchain_barrier(vk::CommandBuffer command, vk::Image image, bool present) {
    const auto color = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    const auto writes = vk::AccessFlagBits2::eColorAttachmentWrite;
    vk::ImageMemoryBarrier2 barrier;
    barrier.setImage(image)
        .setOldLayout(present ? vk::ImageLayout::eColorAttachmentOptimal
                              : vk::ImageLayout::eUndefined)
        .setNewLayout(present ? vk::ImageLayout::ePresentSrcKHR
                              : vk::ImageLayout::eColorAttachmentOptimal)
        .setSrcStageMask(color)
        .setDstStageMask(color)
        .setSrcAccessMask(present ? writes : vk::AccessFlags2{})
        .setDstAccessMask(present ? vk::AccessFlags2{}
                                  : writes | vk::AccessFlagBits2::eColorAttachmentRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    command.pipelineBarrier2(vk::DependencyInfo({}, {}, {}, barrier));
}

} // namespace

void Renderer::Impl::ensure_output() {
    if (!output) {
        output = make_image(device, {{surface_extent.width, surface_extent.height, 1},
                                     color_format,
                                     vk::ImageUsageFlagBits::eColorAttachment |
                                         vk::ImageUsageFlagBits::eTransferSrc,
                                     false});
    }
}

void Renderer::Impl::prepare_output(const FrameInput& input) {
    direct_frame = direct_output && !input.capture_scene && !input.capture_output;
    output_ready = false;
    if (!direct_frame) {
        ensure_output();
    }
    status.direct_output = direct_frame;
}

vk::Image Renderer::Impl::output_image() const {
    return direct_frame ? swap_images[image_index] : *output->handle;
}

vk::ImageView Renderer::Impl::output_view() const {
    return direct_frame ? *swap_views[image_index] : *output->view;
}

void Renderer::Impl::begin_output(vk::AttachmentLoadOp load) {
    if (load == vk::AttachmentLoadOp::eClear) {
        if (direct_frame) {
            swapchain_barrier(command, output_image(), false);
        } else {
            image_barrier(command, output_image(), vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eColorAttachmentOptimal);
        }
    }
    vk::RenderingAttachmentInfo attachment;
    attachment.setImageView(output_view())
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setLoadOp(load)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 1}));
    vk::RenderingInfo rendering;
    rendering.setRenderArea(vk::Rect2D({}, surface_extent))
        .setLayerCount(1)
        .setColorAttachments(attachment);
    command.beginRendering(rendering);
}

void Renderer::Impl::present_output() {
    if (direct_frame) {
        swapchain_barrier(command, output_image(), true);
        return;
    }
    image_barrier(command, *output->handle, vk::ImageLayout::eColorAttachmentOptimal,
                  vk::ImageLayout::eTransferSrcOptimal);
    image_barrier(command, swap_images[image_index], vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eTransferDstOptimal);
    const vk::ImageSubresourceLayers layer(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    const vk::ImageCopy copy(layer, {}, layer, {},
                             vk::Extent3D(surface_extent.width, surface_extent.height, 1));
    command.copyImage(*output->handle, vk::ImageLayout::eTransferSrcOptimal,
                      swap_images[image_index], vk::ImageLayout::eTransferDstOptimal, copy);
    image_barrier(command, swap_images[image_index], vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageLayout::ePresentSrcKHR);
    output_ready = true;
}

} // namespace engine::rendering::vulkan
