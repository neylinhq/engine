#include <cmath>
#include <engine/interaction/input/input.hpp>
namespace engine::interaction::input {
void Router::set_capture(bool captured) {
    if (state_.captured != captured) {
        cursor_valid_ = false;
        pointer_armed_ = false;
    }
    state_.captured = captured;
}
void Router::rebase_pointer() {
    cursor_valid_ = false;
}
RoutingState Router::state() const {
    return state_;
}
void Router::synchronize(const Snapshot& snapshot) {
    previous_ = snapshot.keys;
    previous_left_ = snapshot.pointer.left;
    previous_right_ = snapshot.pointer.right;
    pointer_armed_ = !previous_left_ && !previous_right_;
    cursor_x_ = snapshot.pointer.x;
    cursor_y_ = snapshot.pointer.y;
    cursor_valid_ = false;
}
void Router::buttons(const Snapshot& snapshot, RepeatPolicy repeat, Actions& actions) {
    const auto& mouse = snapshot.pointer;
    const auto left_edge = mouse.left && !previous_left_;
    const auto right_edge = mouse.right && !previous_right_;
    previous_left_ = mouse.left;
    previous_right_ = mouse.right;
    const auto held = mouse.left || mouse.right;
    if (!held) {
        pointer_armed_ = true;
    }
    if (!state_.captured && pointer_armed_ && !snapshot.access.pointer_owned &&
        !snapshot.down(Key::escape) && (left_edge || right_edge)) {
        set_capture(true);
        actions.captured_now = true;
    }
    if (!state_.captured || snapshot.access.pointer_owned || snapshot.access.text_editing) {
        pointer_armed_ = !held;
        return;
    }
    if (!pointer_armed_) {
        return;
    }
    const auto repeating = state_.edit_mode == EditMode::brush ? repeat.brush : repeat.module;
    actions.erase = repeating ? mouse.left : left_edge;
    actions.assign = !actions.erase && (repeating ? mouse.right : right_edge);
}
void Router::pointer(const Snapshot& snapshot, RepeatPolicy repeat, Actions& actions) {
    buttons(snapshot, repeat, actions);
    const auto& mouse = snapshot.pointer;
    const bool available = state_.captured && !snapshot.access.pointer_owned;
    if (available && cursor_valid_) {
        actions.look_x = mouse.x - cursor_x_;
        actions.look_y = mouse.y - cursor_y_;
    }
    cursor_x_ = mouse.x;
    cursor_y_ = mouse.y;
    cursor_valid_ = available;
    if (available && pointer_armed_ && state_.edit_mode == EditMode::brush) {
        actions.wheel = mouse.wheel;
    }
}
void Router::navigation(const Snapshot& snapshot, Actions& actions) const {
    actions.alt = snapshot.down(Key::left_alt) || snapshot.down(Key::right_alt);
    actions.release_guides = snapshot.down(Key::v);
    if (snapshot.access.keyboard_owned) {
        return;
    }
    actions.sun_delta =
        static_cast<int>(snapshot.down(Key::right)) - static_cast<int>(snapshot.down(Key::left));
    actions.pause_sampling = snapshot.down(Key::p);
    actions.forward =
        static_cast<int>(snapshot.down(Key::w)) - static_cast<int>(snapshot.down(Key::s));
    actions.right =
        static_cast<int>(snapshot.down(Key::d)) - static_cast<int>(snapshot.down(Key::a));
    actions.boost = snapshot.down(Key::left_shift) || snapshot.down(Key::right_shift);
    const auto descend = snapshot.down(Key::left_control) || snapshot.down(Key::right_control);
    const auto ascend = snapshot.down(Key::space);
    actions.vertical = static_cast<int>(ascend) - static_cast<int>(descend);
}
std::expected<Actions, std::string> Router::poll(const Snapshot& snapshot, RepeatPolicy repeat) {
    Actions actions;
    if (!snapshot.access.interactive) {
        return actions;
    }
    if (!snapshot.focused) {
        set_capture(false);
        synchronize(snapshot);
        return actions;
    }
    const auto& mouse = snapshot.pointer;
    if (!std::isfinite(mouse.x) || !std::isfinite(mouse.y) || !std::isfinite(mouse.wheel) ||
        (cursor_valid_ &&
         (!std::isfinite(mouse.x - cursor_x_) || !std::isfinite(mouse.y - cursor_y_)))) {
        return std::unexpected("Pointer snapshot contains nonfinite coordinates");
    }
    commands(snapshot, actions);
    pointer(snapshot, repeat, actions);
    navigation(snapshot, actions);
    return actions;
}
} // namespace engine::interaction::input
