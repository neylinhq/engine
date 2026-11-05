#include "../state.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <cstdlib>
#include <imgui.h>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
std::filesystem::path windows_directory() {
#ifdef _MSC_VER
    wchar_t* raw = nullptr;
    std::size_t count = 0;
    if (_wdupenv_s(&raw, &count, L"WINDIR") != 0 || !raw) {
        return L"C:/Windows";
    }
    const std::unique_ptr<wchar_t, decltype(&std::free)> value(raw, &std::free);
    return value.get();
#else
    const auto* directory = std::getenv("WINDIR");
    return directory ? directory : "C:/Windows";
#endif
}

void load_fonts() {
    auto& atlas = *ImGui::GetIO().Fonts;
    atlas.AddFontDefault();
    const auto windows = windows_directory();
    const std::array paths{
        windows / "Fonts/segoeui.ttf", windows / "Fonts/arial.ttf",
        std::filesystem::path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        std::filesystem::path("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"),
        std::filesystem::path("/System/Library/Fonts/Supplemental/Arial.ttf")};
    ImFontConfig configuration;
    configuration.MergeMode = true;
    configuration.PixelSnapH = true;
    for (const auto& path : paths) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            continue;
        }
        const auto name = path_text(path);
        if (atlas.AddFontFromFileTTF(name.c_str(), 13.0f, &configuration,
                                     atlas.GetGlyphRangesCyrillic())) {
            return;
        }
    }
}

void check_result(VkResult result) {
    if (result < VK_SUCCESS) {
        throw std::runtime_error("Dear ImGui Vulkan operation failed: " + std::to_string(result));
    }
}
} // namespace

void Renderer::Impl::initialize_ui() {
    ImGui::SetCurrentContext(context);
    if (!ui_platform) {
        load_fonts();
        ui_platform =
            ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window.native_handle()), true);
        if (!ui_platform) {
            throw std::runtime_error("Dear ImGui GLFW initialization failed");
        }
    }
    if (!swapchain || ui_vulkan) {
        return;
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = *device.instance;
    info.PhysicalDevice = device.physical;
    info.Device = *device.logical;
    info.QueueFamily = device.family;
    info.Queue = device.queue;
    info.MinImageCount = minimum_images;
    info.ImageCount = static_cast<std::uint32_t>(swap_images.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.DescriptorPoolSize = 32;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &ui_format;
    info.CheckVkResultFn = check_result;
    ui_vulkan = ImGui_ImplVulkan_Init(&info);
    if (!ui_vulkan) {
        throw std::runtime_error("Dear ImGui Vulkan initialization failed");
    }
}

void Renderer::Impl::shutdown_ui_renderer() {
    if (ui_vulkan) {
        ImGui_ImplVulkan_Shutdown();
        ui_vulkan = false;
    }
}

} // namespace engine::rendering::vulkan
