#include <engine/rendering/vulkan/renderer.hpp>

#include "state.hpp"
#include "world/compute.hpp"
#include "world/updates.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <limits>
#include <stdexcept>

namespace engine::rendering::vulkan {

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;
Renderer::~Renderer() = default;

std::expected<Renderer, std::string> Renderer::create(platform::window::Window& window,
                                                      Configuration configuration) {
    try {
        return Renderer(std::make_unique<Impl>(window, std::move(configuration)));
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<bool, std::string> Renderer::begin_frame() {
    try {
        return impl_->begin_frame();
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<void, std::string> Renderer::draw(const FrameInput& input) {
    try {
        impl_->draw(input);
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

void Renderer::reset_accumulation() {
    impl_->reset = true;
}

void Renderer::restart() {
    impl_->restart_requested = true;
}

unsigned Renderer::history_index(settings::RenderVersion next_mode, bool paused) const {
    const auto current = next_mode == impl_->mode && !impl_->restart_requested ? impl_->serial : 0;
    const auto next = current + static_cast<std::uint64_t>(!paused);
    return 127u - static_cast<unsigned>(next % 128u);
}

Statistics Renderer::statistics() const {
    auto result = impl_->status;
    result.allocated_bytes = impl_->allocated_bytes();
    result.validation_errors = impl_->device.validation_errors.load(std::memory_order_relaxed);
    result.gpu_generated_world = impl_->gpu_world;
    for (const auto& [name, count] : impl_->world_compute->execution_counts()) {
        static_cast<void>(name);
        result.world_compute_passes += count;
    }
    return result;
}

std::expected<void, std::string> Renderer::screenshot(const std::filesystem::path& path) {
    try {
        impl_->capture(path);
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

Renderer::Impl::Impl(platform::window::Window& window_value, Configuration configuration_value)
    : window(window_value), configuration(std::move(configuration_value)),
      device(window, configuration.validation, configuration.chunk_layout,
             configuration.application_name) {
    status.device = device.properties.deviceName.data();
    status.chunk_layout = device.packed_chunks ? ChunkLayout::packed : ChunkLayout::split;
    initialize_commands();
    world_compute = std::make_unique<WorldCompute>(device, configuration.shaders, buffers, chunks);
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGui::GetIO().IniFilename = nullptr;
    try {
        initialize_ui();
        rebuild_surface();
    } catch (...) {
        shutdown_ui_renderer();
        if (ui_platform) {
            ImGui_ImplGlfw_Shutdown();
        }
        ImGui::DestroyContext(context);
        context = nullptr;
        throw;
    }
}

Renderer::Impl::~Impl() {
    try {
        device.logical->waitIdle();
    } catch (const vk::SystemError&) {
    }
    if (context) {
        ImGui::SetCurrentContext(context);
        shutdown_ui_renderer();
        if (ui_platform) {
            ImGui_ImplGlfw_Shutdown();
        }
        ImGui::DestroyContext(context);
    }
}

void Renderer::Impl::initialize_commands() {
    command_pool = device.logical->createCommandPoolUnique(vk::CommandPoolCreateInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, device.family));
    command = device.logical
                  ->allocateCommandBuffers(vk::CommandBufferAllocateInfo(
                      *command_pool, vk::CommandBufferLevel::ePrimary, 1))
                  .front();
    completion =
        device.logical->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
    for (auto& semaphore : available) {
        semaphore = device.logical->createSemaphoreUnique({});
    }
    if (device.timestamp_bits) {
        timestamps = device.logical->createQueryPoolUnique(
            vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, timestamp_capacity()));
        status.gpu_timestamps_supported = true;
        status.world_updates_profiled =
            configuration.profile_world_updates || configuration.profile_render_passes;
        status.render_passes_profiled = configuration.profile_render_passes;
        timed_passes.reserve(timestamp_capacity());
        status.gpu_passes.reserve(timestamp_capacity());
    }
}

void Renderer::Impl::complete_frame() {
    const auto result =
        device.logical->waitForFences(*completion, true, std::numeric_limits<std::uint64_t>::max());
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Waiting for the render frame failed");
    }
    collect();
    world_compute->release_completed();
    uploads.clear();
}

vk::Semaphore Renderer::Impl::acquisition_semaphore() const {
    // The preceding draw completed the last submission that used this slot.
    return *available[status.frame_count % available.size()];
}

bool Renderer::Impl::begin_frame() {
    if (acquired) {
        throw std::logic_error("A render frame is already acquired");
    }
    const auto size = window.framebuffer();
    if (!size.width || !size.height) {
        return false;
    }
    if (!configuration.defer_frame_wait) {
        complete_frame();
    }
    if (resize || size.width != surface_extent.width || size.height != surface_extent.height) {
        rebuild_surface();
    }
    try {
        const auto acquired_image = device.logical->acquireNextImageKHR(
            *swapchain, std::numeric_limits<std::uint64_t>::max(), acquisition_semaphore(), {});
        image_index = acquired_image.value;
        resize = acquired_image.result == vk::Result::eSuboptimalKHR;
    } catch (const vk::OutOfDateKHRError&) {
        resize = true;
        return false;
    }
    acquired = true;
    ImGui::SetCurrentContext(context);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

} // namespace engine::rendering::vulkan
