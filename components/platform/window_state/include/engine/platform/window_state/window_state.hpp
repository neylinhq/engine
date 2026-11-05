#ifndef ENGINE_PLATFORM_WINDOW_STATE_WINDOW_STATE_HPP
#define ENGINE_PLATFORM_WINDOW_STATE_WINDOW_STATE_HPP
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
namespace engine::platform::window_state {
struct Rectangle {
    int x = 0;
    int y = 0;
    int width = 1600;
    int height = 900;
    bool operator==(const Rectangle&) const = default;
};
struct Monitor {
    std::uint64_t id = 0;
    Rectangle full;
    Rectangle work;
    double desktop_scale_x = 1;
    double desktop_scale_y = 1;
};
struct Placement {
    Rectangle rectangle;
    Rectangle restore_rectangle;
    std::uint64_t monitor = 0;
    double desktop_scale_x = 1;
    double desktop_scale_y = 1;
    bool maximized = false;
    bool decorated = true;
    bool resizable = true;
};
struct Transition {
    Placement target;
    Placement ordinary;
    bool borderless = false;
    std::uint64_t revision = 0;
};
[[nodiscard]] std::expected<std::size_t, std::string>
choose_monitor(Rectangle window, std::span<const Monitor> monitors);
class WindowState {
  public:
    [[nodiscard]] std::expected<Transition, std::string>
    toggle(Placement current, std::span<const Monitor> monitors) const;
    [[nodiscard]] std::expected<void, std::string> commit(const Transition& transition);
    [[nodiscard]] bool borderless() const;
    [[nodiscard]] std::uint64_t revision() const;

  private:
    std::optional<Placement> ordinary_;
    std::uint64_t revision_ = 0;
};
} // namespace engine::platform::window_state
#endif
