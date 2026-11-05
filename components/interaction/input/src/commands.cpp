#include <engine/interaction/input/input.hpp>
namespace engine::interaction::input {
std::string_view command_id(Command command) {
    constexpr std::array<std::string_view, kCommandCount> kIds{
        "command.debug.workspace.toggle",
        "command.platform.pointer.release",
        "command.world.editing.toggle_mode",
        "command.world.editing.undo",
        "command.rendering.shaders.reload",
        "command.rendering.sampling.compare",
        "command.platform.window.toggle_borderless",
        "command.camera.path.toggle"};
    const auto index = static_cast<std::size_t>(command);
    return index < kIds.size() ? kIds[index] : std::string_view{};
}
std::string_view command_key(Command command) {
    constexpr std::array<std::string_view, kCommandCount> kKeys{"Tab", "Esc", "",    "Z",
                                                                "F5",  "F7",  "F11", "F2"};
    const auto index = static_cast<std::size_t>(command);
    return index < kKeys.size() ? kKeys[index] : std::string_view{};
}
bool Snapshot::down(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    return index < keys.size() && keys[index];
}
bool Actions::wants(Command command) const {
    const auto index = static_cast<std::size_t>(command);
    return index < requested.size() && requested[index];
}
bool Router::edge(Key key, bool down) {
    auto& previous = previous_[static_cast<std::size_t>(key)];
    const auto pressed = down && !previous;
    previous = down;
    return pressed;
}
bool Router::poll_window(bool down, bool interactive) {
    return interactive && edge(Key::f11, down);
}
bool Router::apply(Command command) {
    if (command == Command::toggle_debug) {
        state_.debug_open = !state_.debug_open;
        set_capture(!state_.debug_open);
    } else if (command == Command::release_pointer) {
        set_capture(false);
    } else if (command == Command::toggle_edit_mode) {
        state_.edit_mode = state_.edit_mode == EditMode::brush ? EditMode::module : EditMode::brush;
    } else {
        return false;
    }
    return true;
}
void Router::commands(const Snapshot& snapshot, Actions& actions) {
    const auto tab = edge(Key::tab, snapshot.down(Key::tab));
    const auto f1 = edge(Key::f1, snapshot.down(Key::f1));
    if (tab || f1) {
        static_cast<void>(apply(Command::toggle_debug));
    }
    if (edge(Key::escape, snapshot.down(Key::escape))) {
        static_cast<void>(apply(Command::release_pointer));
    }
    if (!snapshot.access.keyboard_owned) {
        actions.requested[static_cast<std::size_t>(Command::undo)] =
            edge(Key::z, snapshot.down(Key::z));
        actions.requested[static_cast<std::size_t>(Command::reload_shaders)] =
            edge(Key::f5, snapshot.down(Key::f5));
    }
    actions.requested[static_cast<std::size_t>(Command::toggle_path)] =
        edge(Key::f2, !snapshot.access.text_editing && snapshot.down(Key::f2));
    actions.requested[static_cast<std::size_t>(Command::toggle_sampling)] =
        edge(Key::f7, !snapshot.access.text_editing && snapshot.down(Key::f7));
    actions.requested[static_cast<std::size_t>(Command::toggle_window)] =
        poll_window(snapshot.down(Key::f11), snapshot.access.interactive);
}
} // namespace engine::interaction::input
