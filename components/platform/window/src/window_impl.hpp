#ifndef ENGINE_PLATFORM_WINDOW_WINDOW_IMPL_HPP
#define ENGINE_PLATFORM_WINDOW_WINDOW_IMPL_HPP
#include <GLFW/glfw3.h>
#include <engine/platform/window/window.hpp>
#include <utility>
namespace engine::platform::window {
struct NativeMonitor {
    GLFWmonitor* native;
    std::uint64_t id;
};
struct Window::Impl {
    GLFWwindow* handle = nullptr;
    bool initialized = false;
    bool captured = false;
    bool transitioning = false;
    double wheel = 0;
    window_state::WindowState state;
    window_state::Rectangle normal;
    std::vector<NativeMonitor> known;
    std::uint64_t next_monitor = 1;
    ~Impl();
    [[nodiscard]] std::uint64_t monitor_id(GLFWmonitor* native);
    [[nodiscard]] std::vector<window_state::Monitor> monitors();
    [[nodiscard]] window_state::Placement placement();
    void place(const window_state::Placement& target);
    void track_normal();
    void reconcile_monitors();
};
[[nodiscard]] std::string last_error(const char* operation);
} // namespace engine::platform::window
#endif
