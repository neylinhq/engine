#include <array>
#include <engine/platform/window_state/window_state.hpp>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void exercise() {
    using engine::platform::window_state::Monitor;
    using engine::platform::window_state::Placement;
    using engine::platform::window_state::WindowState;
    const std::array monitors{Monitor{1, {-1920, 0, 1920, 1080}, {-1920, 0, 1920, 1040}},
                              Monitor{2, {0, 0, 2560, 1440}, {0, 0, 2560, 1400}}};
    require(*engine::platform::window_state::choose_monitor({-1800, 100, 1200, 800}, monitors) == 0,
            "Signed monitor coordinates were lost");
    WindowState state;
    const Placement original{{-1800, 100, 1200, 800}, {-1800, 100, 1200, 800}, 1, 1, 1, true};
    const auto enter = state.toggle(original, monitors);
    require(enter.has_value() && enter->target.rectangle == monitors[0].full &&
                !enter->target.decorated && !enter->target.resizable,
            "Borderless geometry wrong");
    require(!state.borderless(), "Planning changed applied window state");
    require(state.commit(*enter).has_value() && state.borderless(), "Transition commit failed");
    require(!state.commit(*enter).has_value(), "Stale transition committed twice");
    const auto leave = state.toggle(enter->target, monitors);
    require(leave.has_value() && leave->target.rectangle == original.restore_rectangle &&
                leave->target.maximized,
            "Ordinary geometry/maximization lost");
    const std::array remaining{monitors[1]};
    const auto recovered = state.toggle(enter->target, remaining);
    require(recovered.has_value() && recovered->target.rectangle.x >= 0 &&
                recovered->target.maximized,
            "Removed monitor made restore invisible");
    require(state.commit(*recovered).has_value() && !state.borderless(), "Restore commit failed");
}
} // namespace
int main() {
    try {
        exercise();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
