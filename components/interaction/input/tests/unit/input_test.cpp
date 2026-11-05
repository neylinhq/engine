#include <engine/interaction/input/input.hpp>
#include <iostream>
#include <stdexcept>
namespace {
using engine::interaction::input::Command;
using engine::interaction::input::Key;
using engine::interaction::input::Router;
using engine::interaction::input::Snapshot;
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void set(Snapshot& snapshot, Key key, bool down) {
    snapshot.keys[static_cast<std::size_t>(key)] = down;
}
void capture() {
    Router router;
    Snapshot snapshot;
    snapshot.pointer = {200, 400, 0, true, false};
    auto first = router.poll(snapshot);
    require(first.has_value() && first->captured_now && !first->erase && first->look_x == 0,
            "Capture click modified geometry or rotated camera");
    snapshot.pointer.x += 10;
    auto held = router.poll(snapshot);
    require(held.has_value() && !held->erase && held->look_x == 10,
            "The held capture click must remain consumed while the camera can look");
    snapshot.pointer.left = false;
    static_cast<void>(router.poll(snapshot));
    snapshot.pointer.left = true;
    require(router.poll(snapshot)->erase && router.poll(snapshot)->erase,
            "A fresh brush click after release must edit and repeat");
    Router single;
    require(!single.poll(snapshot, {false, false})->erase, "Single capture edited");
    require(!single.poll(snapshot, {false, false})->erase, "Consumed capture edge reappeared");
    snapshot.pointer.left = false;
    static_cast<void>(single.poll(snapshot, {false, false}));
    snapshot.pointer.left = true;
    require(single.poll(snapshot, {false, false})->erase, "New single press was lost");
    set(snapshot, Key::tab, true);
    set(snapshot, Key::escape, true);
    const auto combined = router.poll(snapshot);
    require(combined.has_value() && !router.state().captured && !router.state().debug_open &&
                !combined->captured_now && !combined->erase,
            "Escape must not immediately recapture a held button");
    router.synchronize(snapshot);
    const auto rebased = router.poll(snapshot);
    require(rebased.has_value() && rebased->look_x == 0, "Window transition created mouse delta");
}
void pointer_ownership() {
    Router router;
    Snapshot snapshot;
    snapshot.access.pointer_owned = true;
    snapshot.pointer.right = true;
    require(!router.poll(snapshot)->captured_now, "A UI click cannot capture the scene");
    snapshot.access.pointer_owned = false;
    require(!router.poll(snapshot)->captured_now, "Dragging out of UI cannot capture the scene");
    snapshot.pointer.right = false;
    static_cast<void>(router.poll(snapshot));
    snapshot.pointer.right = true;
    require(router.poll(snapshot)->captured_now, "A fresh scene click must capture");
    snapshot.pointer.left = true;
    require(!router.poll(snapshot)->erase, "A capture chord must wait for both buttons to release");
    snapshot.pointer.right = false;
    require(!router.poll(snapshot)->erase, "Releasing only one button must not enable editing");
    snapshot.pointer.left = false;
    static_cast<void>(router.poll(snapshot));
    snapshot.pointer.right = true;
    require(router.poll(snapshot)->assign, "A fresh right click must assign after capture");
    set(snapshot, Key::escape, true);
    require(!router.poll(snapshot)->captured_now && !router.state().captured,
            "Escape releases a captured drag without reacquiring");
    set(snapshot, Key::escape, false);
    require(!router.poll(snapshot)->captured_now, "Held drag cannot reacquire after Escape");
}
void focus_and_capture_commands() {
    Router router;
    Snapshot snapshot;
    set(snapshot, Key::tab, true);
    snapshot.pointer.left = true;
    require(!router.poll(snapshot)->erase && router.state().captured,
            "Keyboard capture must consume buttons already down");
    require(!router.poll(snapshot)->erase, "Keyboard capture cannot leak a held brush");
    snapshot.focused = false;
    set(snapshot, Key::w, true);
    const auto lost = router.poll(snapshot).value();
    require(!router.state().captured && !lost.erase && lost.forward == 0,
            "An unfocused window must release capture and produce no actions");
    snapshot.focused = true;
    require(!router.poll(snapshot)->captured_now, "Refocus cannot resume the old gesture");
    snapshot.pointer.left = false;
    static_cast<void>(router.poll(snapshot));
    snapshot.pointer.left = true;
    require(router.poll(snapshot)->captured_now && !router.poll(snapshot)->erase,
            "Refocus needs a fresh capture gesture and release before editing");
}
void blocked_edges() {
    Router router;
    Snapshot snapshot;
    snapshot.access.keyboard_owned = true;
    set(snapshot, Key::z, true);
    set(snapshot, Key::f5, true);
    set(snapshot, Key::f7, true);
    set(snapshot, Key::f11, true);
    set(snapshot, Key::w, true);
    const auto blocked = router.poll(snapshot);
    require(blocked.has_value() && !blocked->wants(Command::undo) &&
                blocked->wants(Command::toggle_sampling) &&
                blocked->wants(Command::toggle_window) && blocked->forward == 0,
            "Ownership gates confused keyboard and text capture");
    snapshot.access.keyboard_owned = false;
    const auto released = router.poll(snapshot);
    require(released.has_value() && released->wants(Command::undo) &&
                released->wants(Command::reload_shaders) && released->forward == 1,
            "Z/F5 commands must respect keyboard ownership");
    snapshot.access.interactive = false;
    const auto finite = router.poll(snapshot);
    require(finite.has_value() && !finite->wants(Command::undo) && finite->forward == 0,
            "Physical input changed finite scenario");
}
void flight_bindings() {
    Router router;
    Snapshot snapshot;
    set(snapshot, Key::left_shift, true);
    auto actions = router.poll(snapshot).value();
    require(actions.boost && actions.vertical == 0, "Shift must boost flight without descending");
    set(snapshot, Key::left_shift, false);
    set(snapshot, Key::left_control, true);
    actions = router.poll(snapshot).value();
    require(!actions.boost && actions.vertical == -1, "Ctrl must descend without boosting");
    set(snapshot, Key::space, true);
    require(router.poll(snapshot)->vertical == 0, "Space and Ctrl must cancel vertical motion");
}
void modifiers_and_window() {
    Router router;
    Snapshot snapshot;
    set(snapshot, Key::space, true);
    set(snapshot, Key::right_shift, true);
    set(snapshot, Key::right_control, true);
    set(snapshot, Key::f11, true);
    snapshot.access.text_editing = true;
    require(router.poll_window(true, true), "Global F11 was unavailable");
    const auto actions = router.poll(snapshot);
    require(actions.has_value() && actions->vertical == 0 && actions->boost &&
                !actions->wants(Command::toggle_window),
            "Modifier or window double edge");
    require(!router.poll_window(true, true), "Held F11 repeated");
    require(!router.poll_window(false, true) && router.poll_window(true, true),
            "Next F11 press missing");
}
void original_tool_repeat() {
    Router router;
    Snapshot snapshot;
    router.set_capture(true);
    static_cast<void>(router.poll(snapshot));
    snapshot.pointer.left = true;
    require(router.poll(snapshot, {true, false})->erase, "Continuous brush did not start");
    set(snapshot, Key::b, true);
    snapshot.pointer.wheel = 1;
    const auto repeated = router.poll(snapshot, {true, false}).value();
    require(repeated.erase && repeated.wheel == 1 &&
                router.state().edit_mode == engine::interaction::input::EditMode::brush,
            "Inactive engine module shortcut changed the original NAADF tool behavior");
}
void window_transition_snapshot() {
    Router router;
    Snapshot snapshot;
    router.set_capture(true);
    snapshot.pointer = {200, 400, 0, true, false};
    set(snapshot, Key::f11, true);
    require(router.poll_window(true, true), "Pre-frame F11 transition was not detected");
    router.synchronize(snapshot);
    snapshot.pointer.x += 100;
    const auto rebased = router.poll(snapshot).value();
    require(!rebased.wants(Command::toggle_window) && rebased.look_x == 0 && !rebased.erase,
            "Window transition replayed F11, cursor movement or held editing input");
}
} // namespace
int main() {
    try {
        capture();
        pointer_ownership();
        focus_and_capture_commands();
        blocked_edges();
        modifiers_and_window();
        flight_bindings();
        original_tool_repeat();
        window_transition_snapshot();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
