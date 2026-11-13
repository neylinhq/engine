#ifndef ENGINE_INTERACTION_INPUT_INPUT_HPP
#define ENGINE_INTERACTION_INPUT_INPUT_HPP
#include <array>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
namespace engine::interaction::input {
enum class Key {
    w,
    s,
    a,
    d,
    space,
    left_shift,
    right_shift,
    left_control,
    right_control,
    left_alt,
    right_alt,
    b,
    z,
    f,
    f1,
    f2,
    f5,
    f7,
    f11,
    tab,
    escape,
    v,
    left,
    right,
    p,
    count
};
inline constexpr auto kKeyCount = static_cast<std::size_t>(Key::count);
enum class Command {
    toggle_debug,
    release_pointer,
    toggle_edit_mode,
    undo,
    reload_shaders,
    toggle_sampling,
    toggle_window,
    toggle_path,
    toggle_navigation,
    count
};
inline constexpr auto kCommandCount = static_cast<std::size_t>(Command::count);
[[nodiscard]] std::string_view command_id(Command command);
[[nodiscard]] std::string_view command_key(Command command);
enum class EditMode { brush, module };
struct Access {
    bool interactive = true;
    bool keyboard_owned = false;
    bool pointer_owned = false;
    bool text_editing = false;
};
struct Pointer {
    double x = 0;
    double y = 0;
    double wheel = 0;
    bool left = false;
    bool right = false;
};
struct Snapshot {
    std::array<bool, kKeyCount> keys{};
    Pointer pointer;
    Access access;
    bool focused = true;
    bool space_pressed = false;
    [[nodiscard]] bool down(Key key) const;
};
struct RoutingState {
    bool captured = false;
    bool debug_open = true;
    EditMode edit_mode = EditMode::brush;
};
struct RepeatPolicy {
    bool brush = true;
    bool module = false;
};
struct Actions {
    std::array<bool, kCommandCount> requested{};
    double forward = 0;
    double right = 0;
    double vertical = 0;
    double look_x = 0;
    double look_y = 0;
    double wheel = 0;
    double sun_delta = 0;
    bool pause_sampling = false;
    bool boost = false;
    bool jump_pressed = false;
    bool alt = false;
    bool release_guides = false;
    bool erase = false;
    bool assign = false;
    bool captured_now = false;
    [[nodiscard]] bool wants(Command command) const;
};
class Router {
  public:
    [[nodiscard]] std::expected<Actions, std::string> poll(const Snapshot& snapshot,
                                                           RepeatPolicy repeat = {});
    [[nodiscard]] bool poll_window(bool down, bool interactive);
    void synchronize(const Snapshot& snapshot);
    void set_capture(bool captured);
    void rebase_pointer();
    // The same owner handler is used by hotkeys, menus and the command registry.
    [[nodiscard]] bool apply(Command command);
    [[nodiscard]] RoutingState state() const;

  private:
    [[nodiscard]] bool edge(Key key, bool down);
    void commands(const Snapshot& snapshot, Actions& actions);
    void buttons(const Snapshot& snapshot, RepeatPolicy repeat, Actions& actions);
    void pointer(const Snapshot& snapshot, RepeatPolicy repeat, Actions& actions);
    void navigation(const Snapshot& snapshot, Actions& actions);
    RoutingState state_;
    std::array<bool, kKeyCount> previous_{};
    bool previous_left_ = false;
    bool previous_right_ = false;
    // Ownership changes consume the whole gesture until both edit buttons are up.
    bool pointer_armed_ = true;
    bool cursor_valid_ = false;
    double cursor_x_ = 0;
    double cursor_y_ = 0;
};
} // namespace engine::interaction::input
#endif
