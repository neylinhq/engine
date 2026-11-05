#include "../../src/state.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Context {
    ImGuiContext* value = ImGui::CreateContext();
    Context() {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(640, 480);
        io.DeltaTime = 1.0F / 60;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Context() {
        ImGui::DestroyContext(value);
    }
};

struct Result {
    bool clicked;
    bool hovered;
    bool mouse_down;
    float wheel;
    bool key_down;
    bool text_queued;
    bool focus_lost;
};

Result frame(std::array<char, 32>& text, bool focus_text = false) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(320, 200));
    ImGui::Begin("Visible input panel", nullptr, ImGuiWindowFlags_NoTitleBar);
    const bool clicked = ImGui::Button("Action", ImVec2(100, 24));
    const bool hovered = ImGui::IsItemHovered();
    if (focus_text) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText("Text", text.data(), text.size());
    ImGui::End();
    const auto& io = ImGui::GetIO();
    const Result result{clicked,
                        hovered,
                        io.MouseDown[0],
                        io.MouseWheel,
                        ImGui::IsKeyDown(ImGuiKey_W),
                        !io.InputQueueCharacters.empty(),
                        io.AppFocusLost};
    ImGui::Render();
    return result;
}

void captured_panel_is_read_only() {
    Context context;
    std::array<char, 32> text{};
    auto& io = ImGui::GetIO();
    frame(text);
    io.AddMousePosEvent(40, 20);
    io.AddMouseButtonEvent(0, true);
    const auto active = frame(text);
    require(active.hovered && active.mouse_down, "The visible UI must initially accept input");
    io.AddInputCharacter('a');
    io.AddMouseWheelEvent(0, 1);
    static_cast<void>(engine::gui::frontend::configure_input(false));
    io.AddMousePosEvent(40, 20);
    io.AddMouseButtonEvent(0, false);
    io.AddKeyEvent(ImGuiKey_W, true);
    io.AddInputCharacter('b');
    io.AddMouseWheelEvent(0, 2);
    const auto blocked = frame(text);
    require(ImGui::GetDrawData()->TotalVtxCount > 0, "Scene capture must keep the panel readable");
    require(!blocked.clicked && !blocked.hovered && !blocked.mouse_down && blocked.wheel == 0 &&
                !blocked.key_down && !blocked.text_queued,
            "Captured input must not hover, click, scroll or type in a visible panel");
    static_cast<void>(engine::gui::frontend::configure_input(true));
    io.AddMousePosEvent(40, 20);
    io.AddMouseButtonEvent(0, false);
    require(!frame(text).clicked, "The previous owner's release must not activate a widget");
    io.AddMouseButtonEvent(0, true);
    frame(text);
    io.AddMouseButtonEvent(0, false);
    require(frame(text).clicked, "A fresh UI click after release must still activate the button");
    require((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0,
            "Capture must preserve unrelated ImGui configuration");
}

void text_focus_is_released() {
    Context context;
    std::array<char, 32> text{};
    auto& io = ImGui::GetIO();
    frame(text, true);
    frame(text);
    io.AddInputCharacter('x');
    frame(text);
    require(std::string_view(text.data()) == "x", "Text field must first acquire keyboard focus");
    static_cast<void>(engine::gui::frontend::configure_input(false));
    io.AddInputCharacter('y');
    frame(text);
    require(!io.WantCaptureKeyboard && std::string_view(text.data()) == "x",
            "Captured camera keys must not modify an active text field");
    static_cast<void>(engine::gui::frontend::configure_input(true));
    io.AddInputCharacter('z');
    frame(text);
    require(std::string_view(text.data()) == "x",
            "Returning the pointer must not restore stale text editing focus");
}

void focus_events_survive_capture() {
    Context context;
    std::array<char, 32> text{};
    auto& io = ImGui::GetIO();
    frame(text);
    static_cast<void>(engine::gui::frontend::configure_input(false));
    io.AddFocusEvent(false);
    require(frame(text).focus_lost, "Scene capture must preserve native focus loss notifications");
    io.AddFocusEvent(true);
    io.AddMousePosEvent(40, 20);
    const auto focused = frame(text);
    require(!focused.focus_lost && !focused.hovered,
            "Refocusing a captured scene must not reactivate the visible panel");
}

} // namespace

int main() {
    try {
        captured_panel_is_read_only();
        text_focus_is_released();
        focus_events_survive_capture();
        std::cout << "Visible ImGui panels reject captured hover, click, wheel and text input\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
