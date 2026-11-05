#include "../../src/state.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
namespace frontend = engine::gui::frontend;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

frontend::Editing active_editing() {
    frontend::Editing editing;
    editing.tool = engine::world::editing::Tool::paint;
    editing.material = 7;
    editing.model = 3;
    editing.stamp_model = 4;
    editing.entity_type = 5;
    editing.radius = 23;
    editing.rotation_speed = 2.5F;
    editing.erase = true;
    editing.continuous = false;
    editing.place_camera = true;
    editing.pick_material = true;
    editing.focus_material = true;
    editing.spawn_amount = {2, 3, 4};
    editing.spawn_begin = 13;
    editing.spawn_type = 6;
    editing.spawning = true;
    return editing;
}

void verify_editing_reset(const frontend::Editing& editing, const frontend::Editing& previous) {
    const frontend::Editing defaults;
    require(editing.model == previous.model && editing.spawn_amount == previous.spawn_amount &&
                editing.rotation_speed == previous.rotation_speed,
            "World replacement must preserve model and entity spawning preferences");
    require(editing.tool == defaults.tool && editing.material == defaults.material &&
                editing.stamp_model == defaults.stamp_model &&
                editing.entity_type == defaults.entity_type && editing.radius == defaults.radius,
            "World replacement retained editing references to the previous scene");
    require(editing.erase == defaults.erase && editing.continuous == defaults.continuous &&
                editing.place_camera == defaults.place_camera &&
                editing.pick_material == defaults.pick_material &&
                editing.focus_material == defaults.focus_material,
            "World replacement retained pending editing actions");
    require(editing.spawn_begin == defaults.spawn_begin &&
                editing.spawn_type == defaults.spawn_type && editing.spawning == defaults.spawning,
            "World replacement retained an in-progress spawn from the previous scene");
}

void replacement_resets_scene_state() {
    frontend::UiState state;
    for (const auto speedup : {0, 128, 512}) {
        state.bounds_speedup = speedup;
        state.editing = active_editing();
        const auto previous = state.editing;
        state.reset_world();
        require(state.bounds_speedup == 64,
                "A replaced world must restore the original 32768-group AADF budget");
        verify_editing_reset(state.editing, previous);
    }
}

void replacement_preserves_user_interface() {
    frontend::UiState state;
    state.panels = {false, false, false, true, false, true};
    state.file.opening = true;
    state.file.operation = frontend::Operation::save_world;
    state.file.directory = "selected_directory";
    state.requests.emplace_back().operation = frontend::Operation::screenshot;
    state.import_options = {2048, 128, "zyx"};
    state.segments = {8, 3, 12};
    state.segment_groups = 8;
    state.selected_node = 3;
    state.screenshot_at_end = true;
    state.linear_path = true;
    state.error = "pending message";
    state.reset_world();
    require(!state.panels.visible && !state.panels.settings && !state.panels.debug &&
                state.panels.sky && !state.panels.world && state.panels.path,
            "World replacement changed panel visibility");
    require(state.file.opening && state.file.operation == frontend::Operation::save_world &&
                state.file.directory == "selected_directory" && state.requests.size() == 1 &&
                state.requests.front().operation == frontend::Operation::screenshot,
            "World replacement changed file selection or pending requests");
    require(state.import_options.max_size == 2048 && state.import_options.palette_size == 128 &&
                state.import_options.axis_permutation == "zyx" &&
                state.segments == std::array<int, 3>{8, 3, 12} && state.segment_groups == 8,
            "World replacement changed import or generation settings");
    require(state.selected_node == 3 && state.screenshot_at_end && state.linear_path &&
                state.error == "pending message",
            "World replacement changed path controls or unrelated UI state");
}
} // namespace

int main() {
    try {
        replacement_resets_scene_state();
        replacement_preserves_user_interface();
        std::cout
            << "World replacement resets AADF budget and editing without changing UI preferences\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
