#ifndef ENGINE_APP_SANDBOX_APPLICATION_HPP
#define ENGINE_APP_SANDBOX_APPLICATION_HPP
#include "options.hpp"
#include <engine/gui/frontend/frontend.hpp>
#include <engine/rendering/vulkan/renderer.hpp>
#include <future>
#include <optional>
namespace engine::app::sandbox {
struct PendingImport {
    gui::frontend::Request request;
    std::future<world::model::Model> task;
};
struct Application {
    Options options;
    world::model::World world;
    rendering::settings::Settings settings;
    std::unique_ptr<platform::window::Window> window;
    std::unique_ptr<rendering::vulkan::Renderer> renderer;
    std::optional<motion::camera::Camera> camera;
    motion::path::Path path;
    interaction::input::Router router;
    std::unique_ptr<gui::frontend::Frontend> ui;
    gui::frontend::Metrics metrics;
    std::optional<PendingImport> importing;
    foundation::geometry::Vec3 brush_position;
    foundation::geometry::Vec3 pointer_ray{0, 0, 1};
    bool left_down = false;
    bool left_pressed = false;
    bool left_released = false;
    bool pointer_available = false;
    bool free_edit = false;
    bool free_edit_gesture = false;
    bool brush_active = false;
    bool captured = false;
    bool paused = false;
    bool active = true;
    bool screenshot_requested = false;
    bool reset = true;
    bool rebase_clock = false;
    unsigned frame = 0;
};
[[nodiscard]] int run(Options options);
void initialize(Application& app);
void generate_world(Application& app, world::model::Model source, world::model::IVec3 requested,
                    int segment_groups);
void frame(Application& app, double seconds);
void handle_requests(Application& app, const std::vector<gui::frontend::Request>& requests);
void poll_import(Application& app);
void update_navigation(Application& app, const interaction::input::Actions& actions,
                       double seconds);
void update_editing(Application& app, const interaction::input::Actions& actions, double seconds);
void update_entities(Application& app);
void reset_camera(Application& app);
void reframe_camera(Application& app);
void load_settings(Application& app);
void save_settings(const Application& app);
void load_path(Application& app, const std::filesystem::path& path);
void save_path(const Application& app, const std::filesystem::path& path);
} // namespace engine::app::sandbox
#endif
