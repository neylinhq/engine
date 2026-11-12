#include "window_impl.hpp"
#include <array>
#include <engine/platform/window/window.hpp>
namespace engine::platform::window {
namespace {
constexpr std::array<int, interaction::input::kKeyCount> kCodes{GLFW_KEY_W,
                                                                GLFW_KEY_S,
                                                                GLFW_KEY_A,
                                                                GLFW_KEY_D,
                                                                GLFW_KEY_SPACE,
                                                                GLFW_KEY_LEFT_SHIFT,
                                                                GLFW_KEY_RIGHT_SHIFT,
                                                                GLFW_KEY_LEFT_CONTROL,
                                                                GLFW_KEY_RIGHT_CONTROL,
                                                                GLFW_KEY_LEFT_ALT,
                                                                GLFW_KEY_RIGHT_ALT,
                                                                GLFW_KEY_B,
                                                                GLFW_KEY_Z,
                                                                GLFW_KEY_F1,
                                                                GLFW_KEY_F2,
                                                                GLFW_KEY_F5,
                                                                GLFW_KEY_F7,
                                                                GLFW_KEY_F11,
                                                                GLFW_KEY_TAB,
                                                                GLFW_KEY_ESCAPE,
                                                                GLFW_KEY_V,
                                                                GLFW_KEY_LEFT,
                                                                GLFW_KEY_RIGHT,
                                                                GLFW_KEY_P};
}
bool Window::key(interaction::input::Key key) const {
    const auto index = static_cast<std::size_t>(key);
    return index < kCodes.size() && glfwGetKey(impl_->handle, kCodes[index]) == GLFW_PRESS;
}
interaction::input::Snapshot Window::input(interaction::input::Access access) {
    interaction::input::Snapshot result;
    result.access = access;
    result.focused = focused();
    for (std::size_t index = 0; index < kCodes.size(); ++index) {
        result.keys[index] = glfwGetKey(impl_->handle, kCodes[index]) == GLFW_PRESS;
    }
    glfwGetCursorPos(impl_->handle, &result.pointer.x, &result.pointer.y);
    result.pointer.left = glfwGetMouseButton(impl_->handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    result.pointer.right = glfwGetMouseButton(impl_->handle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    result.pointer.wheel = std::exchange(impl_->wheel, 0);
    return result;
}
} // namespace engine::platform::window
