#include "window_impl.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace engine::platform::window {
void Window::Impl::track_normal() {
    if (transitioning || state.borderless() || glfwGetWindowAttrib(handle, GLFW_MAXIMIZED) ||
        glfwGetWindowAttrib(handle, GLFW_ICONIFIED)) {
        return;
    }
    glfwGetWindowPos(handle, &normal.x, &normal.y);
    glfwGetWindowSize(handle, &normal.width, &normal.height);
}
std::uint64_t Window::Impl::monitor_id(GLFWmonitor* native) {
    const auto found = std::find_if(known.begin(), known.end(),
                                    [&](const auto& item) { return item.native == native; });
    if (found != known.end()) {
        return found->id;
    }
    if (next_monitor == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("Monitor identity exhausted");
    }
    known.push_back({native, next_monitor++});
    return known.back().id;
}
std::vector<window_state::Monitor> Window::Impl::monitors() {
    int count = 0;
    auto** native = glfwGetMonitors(&count);
    std::vector<window_state::Monitor> result;
    if (!native || count <= 0) {
        known.clear();
        return result;
    }
    for (int i = 0; i < count; ++i) {
        const auto id = monitor_id(native[i]);
        const auto mode = glfwGetVideoMode(native[i]);
        if (!mode) {
            continue;
        }
        window_state::Monitor monitor;
        monitor.id = id;
        glfwGetMonitorPos(native[i], &monitor.full.x, &monitor.full.y);
        monitor.full.width = mode->width;
        monitor.full.height = mode->height;
        glfwGetMonitorWorkarea(native[i], &monitor.work.x, &monitor.work.y, &monitor.work.width,
                               &monitor.work.height);
        // GLFW desktop/window coordinates are screen coordinates, not framebuffer pixels.
        // Do not insert content-scale factors into this coordinate system a second time.
        result.push_back(monitor);
    }
    std::erase_if(known, [&](const auto& item) {
        return std::find(native, native + count, item.native) == native + count;
    });
    return result;
}
window_state::Placement Window::Impl::placement() {
    window_state::Placement result;
    glfwGetWindowPos(handle, &result.rectangle.x, &result.rectangle.y);
    glfwGetWindowSize(handle, &result.rectangle.width, &result.rectangle.height);
    result.restore_rectangle = normal;
    result.maximized = glfwGetWindowAttrib(handle, GLFW_MAXIMIZED) != 0;
    result.decorated = glfwGetWindowAttrib(handle, GLFW_DECORATED) != 0;
    result.resizable = glfwGetWindowAttrib(handle, GLFW_RESIZABLE) != 0;
    const auto available = monitors();
    const auto selected = window_state::choose_monitor(result.rectangle, available);
    if (selected) {
        result.monitor = available[*selected].id;
    }
    return result;
}
void Window::Impl::place(const window_state::Placement& target) {
    glfwRestoreWindow(handle);
    glfwSetWindowAttrib(handle, GLFW_DECORATED, target.decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(handle, GLFW_RESIZABLE, target.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowPos(handle, target.rectangle.x, target.rectangle.y);
    glfwSetWindowSize(handle, target.rectangle.width, target.rectangle.height);
    if (target.maximized) {
        glfwMaximizeWindow(handle);
    }
}
std::expected<void, std::string> Window::toggle_borderless() {
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        return std::unexpected("This GLFW Wayland provider cannot position a borderless window; no "
                               "exclusive-fullscreen substitution");
    }
    const auto previous = impl_->placement();
    const auto available = impl_->monitors();
    const auto transition = impl_->state.toggle(previous, available);
    if (!transition) {
        return std::unexpected(transition.error());
    }
    static_cast<void>(glfwGetError(nullptr));
    impl_->transitioning = true;
    impl_->place(transition->target);
    const auto error = glfwGetError(nullptr);
    if (error != GLFW_NO_ERROR) {
        impl_->place(previous);
        impl_->transitioning = false;
        return std::unexpected(
            "Borderless transition failed; restored last usable placement (GLFW " +
            std::to_string(error) + ")");
    }
    const auto committed = impl_->state.commit(*transition);
    if (!committed) {
        impl_->place(previous);
    }
    impl_->transitioning = false;
    return committed;
}
void Window::Impl::reconcile_monitors() {
    if (!state.borderless() || transitioning || glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        return;
    }
    const auto current = placement();
    const auto available = monitors();
    const auto selected = window_state::choose_monitor(current.rectangle, available);
    if (!selected || current.rectangle == available[*selected].full) {
        return;
    }
    auto target = current;
    target.rectangle = available[*selected].full;
    target.maximized = false;
    transitioning = true;
    place(target);
    transitioning = false;
}
} // namespace engine::platform::window
