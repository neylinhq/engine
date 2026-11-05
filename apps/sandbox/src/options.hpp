#ifndef ENGINE_APP_SANDBOX_OPTIONS_HPP
#define ENGINE_APP_SANDBOX_OPTIONS_HPP
#include <array>
#include <engine/rendering/settings/settings.hpp>
#include <engine/rendering/vulkan/renderer.hpp>
#include <filesystem>
#include <optional>
namespace engine::app::sandbox {
struct Options {
    std::filesystem::path scene;
    std::filesystem::path shaders;
    std::filesystem::path settings = "out/user/settings.json";
    std::filesystem::path screenshot;
    rendering::settings::RenderVersion mode = rendering::settings::RenderVersion::base;
    rendering::vulkan::ChunkLayout chunk_layout = rendering::vulkan::ChunkLayout::automatic;
    int width = 1600;
    int height = 900;
    unsigned frames = 0;
    double voxel_metres = 1;
    std::optional<std::array<double, 5>> camera_metres;
    bool hidden = false;
    bool no_ui = false;
    bool validation = false;
    bool defer_frame_wait = true;
    bool offscreen_output = false;
    bool mode_set = false;
    bool help = false;
};
[[nodiscard]] Options parse_options(int count, char** arguments);
void print_usage();
} // namespace engine::app::sandbox
#endif
