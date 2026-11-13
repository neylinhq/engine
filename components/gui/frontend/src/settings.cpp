#include "state.hpp"
#include <array>
namespace engine::gui::frontend {
namespace {
void albedo_settings(rendering::settings::Albedo& settings) {
    ImGui::SliderInt("Taa max frames", &settings.taa_sample_max_age, 1, 32);
    ImGui::Checkbox("Trace Sun", &settings.check_sun);
    ImGui::Checkbox("TAA Jitter", &settings.taa_jitter);
}
void base_settings(rendering::settings::Base& settings) {
    constexpr auto kLog = ImGuiSliderFlags_Logarithmic;
    ImGui::SliderInt("Bounces", &settings.bounce_count, 0, 10);
    ImGui::SliderInt("Resampling max frames", &settings.global_illumination_max_accumulation, 1,
                     128);
    ImGui::SliderInt("Taa max frames", &settings.taa_sample_max_age, 1, 32);
    ImGui::SliderFloat("Resampling radius", &settings.spatial_resample_size, 2, 500, "%.5g", kLog);
    ImGui::SliderInt("Spatial max visibility", &settings.spatial_visibility_max_depth, 0, 80);
    ImGui::SliderFloat("Denoise Threshold", &settings.denoise_threshold, 0.1F, 500, "%.5g", kLog);
    ImGui::SliderFloat("Varying radius factor", &settings.radius_lit_factor, 0, 1000, "%.3g", kLog);
    ImGui::SliderFloat("Noise suppression", &settings.noise_suppression_factor, 0.01F, 100, "%.3g",
                       kLog);
    ImGui::Checkbox("Varying resampling radius", &settings.varying_resampling_radius);
    ImGui::Checkbox("TAA Jitter", &settings.taa_jitter);
    ImGui::Checkbox("Sample leveling", &settings.sample_leveling);
    ImGui::Checkbox("Skip samples", &settings.skip_samples);
    ImGui::Checkbox("Denoise", &settings.denoise);
    ImGui::Checkbox("Atmosphere interaction", &settings.atmosphere_interaction);
}
void path_settings(rendering::settings::PathTracer& settings) {
    ImGui::SliderInt("Bounces", &settings.bounce_count, 0, 10);
    ImGui::SliderInt("Max Samples", &settings.max_samples, 1, 8192);
    ImGui::Checkbox("TAA Jitter", &settings.taa_jitter);
    ImGui::Checkbox("Atmosphere interaction", &settings.atmosphere_interaction);
}
void render_version(UiState& state, rendering::settings::Render& settings) {
    constexpr std::array kNames{"Albedo", "Base", "PathTracer"};
    if (!ImGui::BeginCombo("Render version", kNames[static_cast<std::size_t>(settings.version)])) {
        return;
    }
    for (std::size_t index = 0; index < kNames.size(); ++index) {
        const auto version = static_cast<rendering::settings::RenderVersion>(index);
        const auto selected = settings.version == version;
        if (ImGui::Selectable(kNames[index], selected)) {
            settings.version = version;
            enqueue(state, Operation::restart_render);
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
}
void render_settings(UiState& state, rendering::settings::Render& settings) {
    ImGui::Checkbox("Show ray steps", &settings.show_steps);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Brighter means more steps in primary ray traversal.");
    }
    render_version(state, settings);
    if (settings.version == rendering::settings::RenderVersion::albedo) {
        albedo_settings(settings.albedo);
    } else if (settings.version == rendering::settings::RenderVersion::base) {
        base_settings(settings.base);
    } else {
        path_settings(settings.path_tracer);
    }
}
void general_settings(rendering::settings::General& settings) {
    ImGui::Checkbox("Lock to 60 fps", &settings.lock_to_60);
    ImGui::SliderFloat("Exposure", &settings.exposure, 0.1F, 10, "%.3g",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Tone Mapping", &settings.tone_mapping, 0.1F, 10, "%.3g",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("FOV", &settings.fov, 1, 120, "%.7g");
}
void settings_tabs(UiState& state, Context& context) {
    if (!ImGui::BeginTabBar("SettingsTabs")) {
        return;
    }
    if (ImGui::BeginTabItem("Render")) {
        render_settings(state, context.settings.render);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("General")) {
        general_settings(context.settings.general);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
} // namespace
void draw_settings(UiState& state, Context& context) {
    if (!state.panels.settings) {
        return;
    }
    ImGui::SetNextWindowSize({400, 500}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({5, 278}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &state.panels.settings)) {
        if (ImGui::Button("Save As")) {
            open_file(state, Operation::save_settings);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.4F);
        settings_tabs(state, context);
        ImGui::PopItemWidth();
    }
    ImGui::End();
}
} // namespace engine::gui::frontend
