#ifndef ENGINE_GUI_FRONTEND_FRONTEND_HPP
#define ENGINE_GUI_FRONTEND_FRONTEND_HPP
#include <array>
#include <engine/interaction/input/input.hpp>
#include <engine/motion/camera/camera.hpp>
#include <engine/motion/path/path.hpp>
#include <engine/platform/window/window.hpp>
#include <engine/rendering/settings/settings.hpp>
#include <engine/world/editing/editing.hpp>
#include <engine/world/storage/storage.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace engine::gui::frontend {
enum class Operation {
    open_world,
    open_fixed,
    save_world,
    import_vox,
    import_vl32,
    import_mesh,
    import_model,
    save_settings,
    restart_render,
    load_path,
    save_path,
    screenshot
};
struct Request {
    Operation operation = Operation::open_world;
    std::filesystem::path path;
    world::storage::ImportOptions import_options;
    world::model::IVec3 requested_size;
    int segment_groups = 4;
};
struct Editing {
    world::editing::Tool tool = world::editing::Tool::none;
    std::uint16_t material = 0xFFFF;
    int model = -1;
    int stamp_model = -1;
    int entity_type = -1;
    float radius = 10;
    float rotation_speed = 1;
    bool erase = false;
    bool continuous = true;
    bool place_camera = false;
    bool pick_material = false;
    bool focus_material = false;
    std::array<int, 3> spawn_amount{1, 1, 1};
    std::size_t spawn_begin = 0;
    std::size_t spawn_type = 0;
    bool spawning = false;
};
struct Metrics {
    double milliseconds = 0;
    double editing_milliseconds = 0;
    double entity_milliseconds = 0;
    double entity_kilobytes = 0;
    double gpu_milliseconds = 0;
    std::uint64_t gpu_bytes = 0;
    unsigned render_width = 0;
    unsigned render_height = 0;
    unsigned samples = 0;
    std::string chunk_storage;
    std::string output_path;
    std::string status;
};
struct Context {
    world::model::World& world;
    motion::camera::Camera& camera;
    motion::path::Path& path;
    rendering::settings::Settings& settings;
    Metrics metrics;
};
class Frontend {
  public:
    explicit Frontend(platform::window::Window& window);
    ~Frontend();
    Frontend(Frontend&&) noexcept;
    Frontend& operator=(Frontend&&) noexcept;
    // Set before polling native events and whenever scene capture changes.
    void accept_input(bool enabled);
    void set_visible(bool visible);
    [[nodiscard]] bool visible() const;
    [[nodiscard]] interaction::input::Access access() const;
    [[nodiscard]] Editing& editing();
    // Reset scene-bound editing state and AADF scheduling after world replacement.
    void reset_world();
    [[nodiscard]] bool screenshot_at_end() const;
    [[nodiscard]] bool linear_path() const;
    [[nodiscard]] int bounds_speedup() const;
    [[nodiscard]] std::vector<Request> draw(Context context);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::gui::frontend
#endif
