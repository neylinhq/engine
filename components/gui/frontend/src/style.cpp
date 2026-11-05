#include "state.hpp"
#include <cmath>
namespace engine::gui::frontend {
namespace {
float linear(float value) {
    return value < 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}
void common_colors(ImGuiStyle& style) {
    const ImVec4 hover{0.6F, 0.4F, 0.1F, 0.25F};
    const ImVec4 neutral{0.5F, 0.5F, 0.5F, 0.5F};
    const ImVec4 active{0.5F, 0.5F, 0.5F, 0.75F};
    for (auto slot : {ImGuiCol_Button, ImGuiCol_Header, ImGuiCol_ResizeGrip, ImGuiCol_ScrollbarGrab,
                      ImGuiCol_SliderGrab, ImGuiCol_Tab}) {
        style.Colors[slot] = neutral;
    }
    for (auto slot :
         {ImGuiCol_ButtonHovered, ImGuiCol_HeaderHovered, ImGuiCol_ResizeGripHovered,
          ImGuiCol_ScrollbarGrabHovered, ImGuiCol_FrameBgHovered, ImGuiCol_TabHovered}) {
        style.Colors[slot] = hover;
    }
    for (auto slot : {ImGuiCol_ButtonActive, ImGuiCol_HeaderActive, ImGuiCol_ResizeGripActive,
                      ImGuiCol_ScrollbarGrabActive, ImGuiCol_SliderGrabActive, ImGuiCol_CheckMark,
                      ImGuiCol_TabSelected}) {
        style.Colors[slot] = active;
    }
}
} // namespace
void apply_style() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {10, 10};
    style.WindowMinSize = {10, 10};
    style.Colors[ImGuiCol_Text] = {1, 1, 1, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.25F, 0.25F, 0.25F, 1};
    style.Colors[ImGuiCol_Border] = {0.25F, 0.25F, 0.25F, 1};
    style.Colors[ImGuiCol_WindowBg] = {0.125F, 0.125F, 0.125F, 0.95F};
    style.Colors[ImGuiCol_MenuBarBg] = {0.062F, 0.062F, 0.062F, 0.877F};
    style.Colors[ImGuiCol_FrameBg] = {0.033F, 0.033F, 0.033F, 0.75F};
    style.Colors[ImGuiCol_FrameBgActive] = style.Colors[ImGuiCol_FrameBg];
    style.Colors[ImGuiCol_Separator] = {0.75F, 0.75F, 0.75F, 0.75F};
    style.Colors[ImGuiCol_TextSelectedBg] = {1, 0.75F, 0.125F, 0.25F};
    common_colors(style);
    for (auto& color : style.Colors) {
        color = {linear(color.x), linear(color.y), linear(color.z), std::pow(color.w, 1.0F / 2.2F)};
    }
}
} // namespace engine::gui::frontend
