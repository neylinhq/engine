#include "../state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
vk::SurfaceFormatKHR choose_format(std::span<const vk::SurfaceFormatKHR> formats) {
    for (const auto desired : {vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb,
                               vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm}) {
        for (const auto format : formats) {
            if (format.format == desired &&
                format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return format;
            }
        }
    }
    throw std::runtime_error("An RGBA8/BGRA8 presentation surface is required");
}

vk::CompositeAlphaFlagBitsKHR choose_alpha(vk::CompositeAlphaFlagsKHR supported) {
    for (const auto alpha :
         {vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
          vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
          vk::CompositeAlphaFlagBitsKHR::eInherit}) {
        if (supported & alpha) {
            return alpha;
        }
    }
    throw std::runtime_error("No supported presentation alpha mode");
}

vk::Extent2D choose_extent(const vk::SurfaceCapabilitiesKHR& capabilities,
                           platform::window::Extent requested) {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    return {std::clamp(requested.width, capabilities.minImageExtent.width,
                       capabilities.maxImageExtent.width),
            std::clamp(requested.height, capabilities.minImageExtent.height,
                       capabilities.maxImageExtent.height)};
}

vk::Extent2D internal_extent(vk::Extent2D requested, const vk::PhysicalDeviceLimits& limits) {
    const auto pixels = static_cast<double>(requested.width) * requested.height;
    const auto maximum_pixels =
        std::min<vk::DeviceSize>(limits.maxStorageBufferRange / (32u * 8u),
                                 vk::DeviceSize(limits.maxComputeWorkGroupCount[0]) * 64u);
    const auto limit = static_cast<double>(maximum_pixels);
    if (pixels <= limit) {
        return requested;
    }
    const auto scale = std::sqrt(limit / pixels);
    return {std::max(1u, static_cast<std::uint32_t>(std::floor(requested.width * scale))),
            std::max(1u, static_cast<std::uint32_t>(std::floor(requested.height * scale)))};
}

vk::PresentModeKHR choose_present(std::span<const vk::PresentModeKHR> modes,
                                  const Configuration& configuration) {
    const auto requested = configuration.presentation_mode;
    if (requested != PresentationMode::automatic) {
        const auto mode = requested == PresentationMode::immediate ? vk::PresentModeKHR::eImmediate
                          : requested == PresentationMode::mailbox ? vk::PresentModeKHR::eMailbox
                                                                   : vk::PresentModeKHR::eFifo;
        if (std::ranges::find(modes, mode) == modes.end()) {
            throw std::runtime_error("Requested Vulkan presentation mode is not supported");
        }
        return mode;
    }
    if (!configuration.vertical_sync &&
        std::ranges::find(modes, vk::PresentModeKHR::eImmediate) != modes.end()) {
        return vk::PresentModeKHR::eImmediate;
    }
    return vk::PresentModeKHR::eFifo;
}
} // namespace

void Renderer::Impl::replace_swapchain(const vk::SurfaceCapabilitiesKHR& capabilities,
                                       const vk::SurfaceFormatKHR& format) {
    const auto modes = device.physical.getSurfacePresentModesKHR(device.surface.handle);
    surface_extent = choose_extent(capabilities, window.framebuffer());
    minimum_images = std::max(2u, capabilities.minImageCount);
    auto count = minimum_images + 1;
    if (capabilities.maxImageCount) {
        count = std::min(count, capabilities.maxImageCount);
    }
    const auto usage = configure_output(capabilities, format.format);
    const auto present_mode = choose_present(modes, configuration);
    status.presentation_mode =
        present_mode == vk::PresentModeKHR::eImmediate ? PresentationMode::immediate
        : present_mode == vk::PresentModeKHR::eMailbox ? PresentationMode::mailbox
                                                       : PresentationMode::fifo;
    vk::SwapchainCreateInfoKHR info;
    info.setSurface(device.surface.handle)
        .setMinImageCount(count)
        .setImageFormat(format.format)
        .setImageColorSpace(format.colorSpace)
        .setImageExtent(surface_extent)
        .setImageArrayLayers(1)
        .setImageUsage(usage)
        .setImageSharingMode(vk::SharingMode::eExclusive)
        .setPreTransform(capabilities.currentTransform)
        .setCompositeAlpha(choose_alpha(capabilities.supportedCompositeAlpha))
        .setPresentMode(present_mode)
        .setClipped(true)
        .setOldSwapchain(swapchain ? *swapchain : vk::SwapchainKHR{});
    auto replacement = device.logical->createSwapchainKHRUnique(info);
    bind_present_images(*replacement, format.format);
    swapchain = std::move(replacement);
}

void Renderer::Impl::bind_present_images(vk::SwapchainKHR replacement, vk::Format format) {
    swap_views.clear();
    swap_images = device.logical->getSwapchainImagesKHR(replacement);
    finished.clear();
    const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    for (const auto image : swap_images) {
        finished.push_back(device.logical->createSemaphoreUnique({}));
        if (direct_output) {
            swap_views.push_back(device.logical->createImageViewUnique(
                vk::ImageViewCreateInfo({}, image, vk::ImageViewType::e2D, format, {}, range)));
        }
    }
}

vk::ImageUsageFlags Renderer::Impl::configure_output(const vk::SurfaceCapabilitiesKHR& capabilities,
                                                     vk::Format format) {
    const bool blue_first =
        format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb;
    color_format = blue_first ? vk::Format::eB8G8R8A8Srgb : vk::Format::eR8G8B8A8Srgb;
    ui_format = static_cast<VkFormat>(color_format);
    direct_output =
        !configuration.offscreen_output && format == color_format &&
        bool(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment);
    status.direct_output = direct_output;
    auto usage = vk::ImageUsageFlags(vk::ImageUsageFlagBits::eTransferDst);
    if (direct_output) {
        usage |= vk::ImageUsageFlagBits::eColorAttachment;
    }
    return usage;
}

void Renderer::Impl::resize_output() {
    render_extent = internal_extent(surface_extent, device.properties.limits);
    output.reset();
    if (!direct_output) {
        ensure_output();
    }
    programs.clear();
    resources_ready = false;
    output_ready = false;
    scene_ready = false;
    scene_output.reset();
    reset = true;
    status.width = render_extent.width;
    status.height = render_extent.height;
}

void Renderer::Impl::rebuild_surface() {
    device.logical->waitIdle();
    complete_frame();
    shutdown_ui_renderer();
    const auto capabilities = device.physical.getSurfaceCapabilitiesKHR(device.surface.handle);
    if (!(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst)) {
        throw std::runtime_error("Presentation surface does not support image transfer");
    }
    const auto format = choose_format(device.physical.getSurfaceFormatsKHR(device.surface.handle));
    replace_swapchain(capabilities, format);
    resize_output();
    initialize_ui();
    resize = false;
}

} // namespace engine::rendering::vulkan
