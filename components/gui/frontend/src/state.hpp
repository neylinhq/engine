#ifndef ENGINE_GUI_FRONTEND_STATE_HPP
#define ENGINE_GUI_FRONTEND_STATE_HPP
#include <engine/gui/frontend/frontend.hpp>
#include <imgui.h>
#include <optional>
namespace engine::gui::frontend {
struct Panels {
    bool visible = true;
    bool settings = true;
    bool debug = true;
    bool sky = false;
    bool world = true;
    bool path = false;
};
struct FileDialog {
    bool opening = false;
    std::optional<Operation> operation;
    std::array<char, 2048> path{};
    std::filesystem::path directory;
    std::filesystem::path overwrite_path;
    std::string error;
};
struct UiState {
    Panels panels;
    Editing editing;
    FileDialog file;
    std::vector<Request> requests;
    world::storage::ImportOptions import_options;
    std::array<int, 3> segments{16, 2, 16};
    int segment_groups = 4;
    int bounds_speedup = 64;
    std::array<char, 4> permutation{'x', 'z', 'y', '\0'};
    int selected_node = -1;
    std::array<char, 256> node_name{};
    std::array<char, 1024> path_name{};
    bool screenshot_at_end = false;
    bool linear_path = false;
    std::string error;

    void reset_world() {
        const auto previous = editing;
        editing = {};
        editing.model = previous.model;
        editing.spawn_amount = previous.spawn_amount;
        editing.rotation_speed = previous.rotation_speed;
        bounds_speedup = 64;
    }
};
struct Frontend::State {
    UiState ui;
    ImGuiContext* context = nullptr;
    platform::window::Window* window = nullptr;
};
// Uses the current ImGui context. Reports a transfer of input ownership.
[[nodiscard]] bool configure_input(bool enabled);
void apply_style();
void draw_menu(UiState& state);
void draw_files(UiState& state);
void draw_settings(UiState& state, Context& context);
void draw_sky(UiState& state, Context& context);
void draw_world(UiState& state, Context& context);
void draw_material(UiState& state, Context& context);
void draw_path(UiState& state, Context& context);
void draw_debug(UiState& state, Context& context);
void open_file(UiState& state, Operation operation);
bool edit_vector(const char* label, foundation::geometry::Vec3& vector);
bool color_vector(const char* label, foundation::geometry::Vec3& vector);
void enqueue(UiState& state, Operation operation);
} // namespace engine::gui::frontend
#endif
