#include "state.hpp"

#include <GLFW/glfw3.h>

namespace engine::gui::frontend {

bool configure_input(bool enabled) {
    auto& io = ImGui::GetIO();
    if (io.AppAcceptingEvents == enabled) {
        return false;
    }
    // Transfer ownership without replaying a queued release or a held UI gesture.
    io.SetAppAcceptingEvents(enabled);
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
    constexpr auto kBlocked = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard |
                              ImGuiConfigFlags_NoMouseCursorChange;
    if (enabled) {
        io.ConfigFlags &= ~kBlocked;
    } else {
        io.ConfigFlags |= kBlocked;
        ImGui::SetWindowFocus(nullptr);
    }
    return true;
}

void Frontend::accept_input(bool enabled) {
    ImGui::SetCurrentContext(state_->context);
    if (!configure_input(enabled) || !enabled) {
        return;
    }
    // Escape can restore a stationary OS cursor without a new cursor-move event.
    double x = 0, y = 0;
    glfwGetCursorPos(static_cast<GLFWwindow*>(state_->window->native_handle()), &x, &y);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
}

} // namespace engine::gui::frontend
