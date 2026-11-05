#include "window_impl.hpp"
#include <algorithm>
#include <bit>
#include <engine/platform/window/window.hpp>
#include <stdexcept>
namespace engine::platform::window {
std::string last_error(const char* operation) {
    const char* description = nullptr;
    const auto code = glfwGetError(&description);
    return std::string(operation) + ": " +
           (description ? description : "no detailed GLFW message") + " (" + std::to_string(code) +
           ")";
}
Window::Impl::~Impl() {
    if (handle) {
        glfwDestroyWindow(handle);
    }
    if (initialized) {
        glfwTerminate();
    }
}
Window::Window(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;
Window::~Window() = default;
std::expected<Window, std::string> Window::create(Configuration configuration) {
    if (configuration.width <= 0 || configuration.height <= 0) {
        return std::unexpected("Invalid window size");
    }
    auto impl = std::make_unique<Impl>();
    if (!glfwInit()) {
        return std::unexpected(last_error("GLFW initialization"));
    }
    impl->initialized = true;
    if (!glfwVulkanSupported()) {
        return std::unexpected("No Vulkan loader or usable Vulkan platform support");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, configuration.visible ? GLFW_TRUE : GLFW_FALSE);
    impl->handle = glfwCreateWindow(configuration.width, configuration.height,
                                    configuration.title.c_str(), nullptr, nullptr);
    if (!impl->handle) {
        return std::unexpected(last_error("Create window"));
    }
    glfwSetWindowUserPointer(impl->handle, impl.get());
    glfwSetScrollCallback(impl->handle, [](GLFWwindow* native, double, double vertical) {
        auto* owner = static_cast<Impl*>(glfwGetWindowUserPointer(native));
        owner->wheel += vertical;
    });
    glfwSetWindowPosCallback(impl->handle, [](GLFWwindow* native, int, int) {
        static_cast<Impl*>(glfwGetWindowUserPointer(native))->track_normal();
    });
    glfwSetWindowSizeCallback(impl->handle, [](GLFWwindow* native, int, int) {
        static_cast<Impl*>(glfwGetWindowUserPointer(native))->track_normal();
    });
    impl->track_normal();
    return Window(std::move(impl));
}
void Window::poll() {
    glfwPollEvents();
    impl_->reconcile_monitors();
}
void Window::wait() {
    glfwWaitEvents();
}
bool Window::closed() const {
    return glfwWindowShouldClose(impl_->handle) != 0;
}
bool Window::minimized() const {
    return glfwGetWindowAttrib(impl_->handle, GLFW_ICONIFIED) != 0;
}
bool Window::focused() const {
    return glfwGetWindowAttrib(impl_->handle, GLFW_FOCUSED) != 0;
}
Extent Window::framebuffer() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl_->handle, &width, &height);
    return {static_cast<std::uint32_t>(width > 0 ? width : 0),
            static_cast<std::uint32_t>(height > 0 ? height : 0)};
}
Extent Window::logical_extent() const {
    int width = 0;
    int height = 0;
    glfwGetWindowSize(impl_->handle, &width, &height);
    return {static_cast<std::uint32_t>(std::max(0, width)),
            static_cast<std::uint32_t>(std::max(0, height))};
}
void Window::capture(bool captured) {
    if (captured == impl_->captured) {
        return;
    }
    glfwSetInputMode(impl_->handle, GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(impl_->handle, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }
    impl_->captured = captured;
}
void* Window::native_handle() const {
    return impl_->handle;
}
void Window::title(const std::string& title) {
    glfwSetWindowTitle(impl_->handle, title.c_str());
}
bool Window::borderless() const {
    return impl_->state.borderless();
}
std::expected<std::vector<const char*>, std::string> Window::instance_extensions() const {
    std::uint32_t count = 0;
    const auto names = glfwGetRequiredInstanceExtensions(&count);
    if (!names || count == 0) {
        return std::unexpected(last_error("Vulkan platform extensions"));
    }
    return std::vector<const char*>(names, names + count);
}
std::expected<std::uint64_t, std::string> Window::create_surface(std::uintptr_t instance) const {
    VkSurfaceKHR surface{};
    const auto status = glfwCreateWindowSurface(reinterpret_cast<VkInstance>(instance),
                                                impl_->handle, nullptr, &surface);
    if (status != VK_SUCCESS) {
        return std::unexpected("Vulkan surface creation failed: " + std::to_string(status));
    }
    static_assert(sizeof(surface) == sizeof(std::uint64_t));
    return std::bit_cast<std::uint64_t>(surface);
}
} // namespace engine::platform::window
