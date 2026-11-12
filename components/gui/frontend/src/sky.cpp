#include "state.hpp"
namespace engine::gui::frontend {
namespace {
void optical_parameters(rendering::settings::Atmosphere& sky) {
    constexpr auto kLog = ImGuiSliderFlags_Logarithmic;
    ImGui::ColorEdit3("Sun Color", sky.sun_color.data(), ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SliderFloat("Sun Intensity", &sky.sun_intensity, 0.1F, 100, "%.3g", kLog);
    ImGui::SliderFloat3("Rayleigh scatter", sky.rayleigh_scatter.data(), 1, 100, "%.2f", kLog);
    ImGui::SliderFloat("Mie scatter", &sky.mie_scatter, 0.1F, 10, "%.1f", kLog);
    ImGui::SliderFloat3("Ozone absorb", sky.ozone_absorb.data(), 0.1F, 10, "%.2f", kLog);
}
void atmosphere_parameters(rendering::settings::Atmosphere& sky) {
    constexpr auto kLog = ImGuiSliderFlags_Logarithmic;
    auto radius = sky.sphere_radius * 0.001F;
    auto thickness = sky.thickness * 0.001F;
    if (ImGui::SliderFloat("Planet radius [km]", &radius, 100, 1000000, "%.1f", kLog)) {
        sky.sphere_radius = radius * 1000;
    }
    if (ImGui::SliderFloat("Atmosphere thickness [km]", &thickness, 0.02F, 100, "%.2f", kLog)) {
        sky.thickness = thickness * 1000;
    }
    ImGui::SliderFloat("Atmosphere average point", &sky.average_point, 0.01F, 1, "%.3f");
    ImGui::SliderFloat("Atmosphere density", &sky.density, 0.01F, 10, "%.2f");
    ImGui::SliderFloat("Absorb intensity", &sky.absorb_intensity, 0.01F, 10, "%.4f", kLog);
    ImGui::SliderFloat("Scatter intensity", &sky.scatter_intensity, 0.01F, 10, "%.4f", kLog);
    ImGui::SliderFloat("Mie factor", &sky.mie_factor, -1, 0.999F, "%.3f");
    ImGui::SliderInt("Main ray steps", &sky.main_ray_steps, 1, 100);
    ImGui::SliderInt("Sub scatter steps", &sky.sub_scatter_steps, 1, 100);
}
} // namespace
void draw_sky(UiState& state, Context& context) {
    if (!state.panels.sky) {
        return;
    }
    ImGui::SetNextWindowSize({250, 350}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({500, 18}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sky Debug", &state.panels.sky)) {
        optical_parameters(context.settings.atmosphere);
        atmosphere_parameters(context.settings.atmosphere);
    }
    ImGui::End();
}
} // namespace engine::gui::frontend
