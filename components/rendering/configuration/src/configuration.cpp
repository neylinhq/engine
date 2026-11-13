#include <engine/rendering/configuration/configuration.hpp>
#include "settings.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace engine::rendering::configuration {
namespace {
void clamp_settings(settings::Settings& settings) {
    auto& general = settings.general;
    general.exposure = std::clamp(general.exposure, 0.1F, 10.0F);
    general.tone_mapping = std::clamp(general.tone_mapping, 0.1F, 10.0F);
    general.fov = std::clamp(general.fov, 1.0F, 120.0F);
    auto& base = settings.render.base;
    base.bounce_count = std::clamp(base.bounce_count, 0, 10);
    base.global_illumination_max_accumulation =
        std::clamp(base.global_illumination_max_accumulation, 1, 128);
    base.taa_sample_max_age = std::clamp(base.taa_sample_max_age, 1, 32);
    base.spatial_resample_size = std::clamp(base.spatial_resample_size, 2.0F, 500.0F);
    base.spatial_visibility_max_depth = std::clamp(base.spatial_visibility_max_depth, 0, 80);
    base.denoise_threshold = std::clamp(base.denoise_threshold, 0.1F, 500.0F);
    base.radius_lit_factor = std::clamp(base.radius_lit_factor, 0.0F, 1000.0F);
    base.noise_suppression_factor = std::clamp(base.noise_suppression_factor, 0.01F, 100.0F);
    settings.render.albedo.taa_sample_max_age =
        std::clamp(settings.render.albedo.taa_sample_max_age, 1, 32);
    auto& path = settings.render.path_tracer;
    path.bounce_count = std::clamp(path.bounce_count, 0, 10);
    path.max_samples = std::clamp(path.max_samples, 1, 8192);
}

void validate_settings(const settings::Settings& settings) {
    const auto& base_settings = settings.render.base;
    const std::array scalars{settings.general.exposure,
                             settings.general.tone_mapping,
                             settings.general.fov,
                             base_settings.spatial_resample_size,
                             base_settings.denoise_threshold,
                             base_settings.radius_lit_factor,
                             base_settings.noise_suppression_factor};
    if (!std::ranges::all_of(scalars, [](float value) { return std::isfinite(value); })) {
        throw std::runtime_error("Render settings must be finite");
    }
    using settings::RenderVersion;
    const auto version = settings.render.version;
    if (version != RenderVersion::albedo && version != RenderVersion::base &&
        version != RenderVersion::path_tracer) {
        throw std::runtime_error("Unknown render version");
    }
    validate_atmosphere(settings.atmosphere);
}
json::object encode_settings(const settings::Settings& settings) {
    json::object general{{"exposure", settings.general.exposure},
                         {"toneMappingFac", settings.general.tone_mapping},
                         {"fov", settings.general.fov},
                         {"lockTo60fps", settings.general.lock_to_60}};
    return {{"general", std::move(general)},
            {"render", write_render(settings.render)},
            {"atmosphere", write_atmosphere(settings.atmosphere)}};
}
void known_keys(const json::object& input, const json::object& known) {
    for (const auto& member : input) {
        const auto* reference = known.if_contains(member.key());
        if (!reference) {
            throw std::runtime_error("Unknown setting: " + std::string(member.key()));
        }
        if (reference->is_object()) {
            known_keys(member.value().as_object(), reference->as_object());
        }
    }
}
} // namespace
settings::Settings checked_settings(const settings::Settings& settings, bool strict) {
    validate_settings(settings);
    auto clamped = settings;
    clamp_settings(clamped);
    if (strict && clamped != settings) {
        throw std::runtime_error("Settings outside supported bounds");
    }
    return clamped;
}
settings::Settings parse(std::string_view text, bool strict) {
    const auto value = json::parse(text);
    const auto& root = value.as_object();
    settings::Settings result;
    if (strict) {
        known_keys(root, encode_settings(result));
    }
    if (const auto* group = root.if_contains("general")) {
        const auto& general = group->as_object();
        read_setting(general, "exposure", result.general.exposure);
        read_setting(general, "toneMappingFac", result.general.tone_mapping);
        read_setting(general, "fov", result.general.fov);
        read_setting(general, "lockTo60fps", result.general.lock_to_60);
    }
    if (const auto* group = root.if_contains("render")) {
        read_render(group->as_object(), result.render);
    }
    if (const auto* group = root.if_contains("atmosphere")) {
        read_atmosphere(group->as_object(), result.atmosphere);
    }
    return checked_settings(result, strict);
}
std::string serialize(const settings::Settings& settings) {
    return json::serialize(encode_settings(settings));
}
} // namespace engine::rendering::configuration
