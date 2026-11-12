#include <engine/gui/frontend/frontend.hpp>

#include "state.hpp"
#include <utility>
namespace engine::gui::frontend {
Frontend::Frontend(platform::window::Window& window) : state_(std::make_unique<State>()) {
    state_->window = &window;
    state_->context = ImGui::GetCurrentContext();
    apply_style();
}
Frontend::~Frontend() = default;
Frontend::Frontend(Frontend&&) noexcept = default;
Frontend& Frontend::operator=(Frontend&&) noexcept = default;
void Frontend::set_visible(bool visible) {
    state_->ui.panels.visible = visible;
}
bool Frontend::visible() const {
    return state_->ui.panels.visible;
}
interaction::input::Access Frontend::access() const {
    ImGui::SetCurrentContext(state_->context);
    const auto& io = ImGui::GetIO();
    const bool available = state_->ui.panels.visible && io.AppAcceptingEvents;
    return {true, available && io.WantCaptureKeyboard, available && io.WantCaptureMouse,
            available && io.WantTextInput};
}
Editing& Frontend::editing() {
    return state_->ui.editing;
}
void Frontend::reset_world() {
    state_->ui.reset_world();
}
bool Frontend::screenshot_at_end() const {
    return state_->ui.screenshot_at_end;
}
bool Frontend::linear_path() const {
    return state_->ui.linear_path;
}
int Frontend::bounds_speedup() const {
    return state_->ui.bounds_speedup;
}
std::vector<Request> Frontend::draw(Context context) {
    ImGui::SetCurrentContext(state_->context);
    auto& state = state_->ui;
    state.requests.clear();
    if (state.panels.visible) {
        draw_menu(state);
        draw_settings(state, context);
        draw_debug(state, context);
        draw_sky(state, context);
        draw_world(state, context);
        draw_path(state, context);
        draw_files(state);
    }
    return std::exchange(state.requests, {});
}
bool edit_vector(const char* label, foundation::geometry::Vec3& vector) {
    float values[]{static_cast<float>(vector.x), static_cast<float>(vector.y),
                   static_cast<float>(vector.z)};
    const auto changed = ImGui::InputFloat3(label, values);
    if (changed) {
        vector = {values[0], values[1], values[2]};
    }
    return changed;
}
bool color_vector(const char* label, foundation::geometry::Vec3& vector) {
    float values[]{static_cast<float>(vector.x), static_cast<float>(vector.y),
                   static_cast<float>(vector.z)};
    const auto changed = ImGui::ColorEdit3(label, values);
    if (changed) {
        vector = {values[0], values[1], values[2]};
    }
    return changed;
}
void enqueue(UiState& state, Operation operation) {
    Request request;
    request.operation = operation;
    state.requests.push_back(std::move(request));
}
} // namespace engine::gui::frontend
