#include "settings.hpp"
namespace engine::rendering::configuration {
namespace {
void read_base(const json::object& object, settings::Base& base) {
    read_setting(object, "isDenoise", base.denoise);
    read_setting(object, "isTAAJitter", base.taa_jitter);
    read_setting(object, "skipSamples", base.skip_samples);
    read_setting(object, "isSampleLeveling", base.sample_leveling);
    read_setting(object, "isVaryingResmaplingRadius", base.varying_resampling_radius);
    read_setting(object, "isAtmosphereInteraction", base.atmosphere_interaction);
    read_setting(object, "taaSampleMaxAge", base.taa_sample_max_age);
    read_setting(object, "bounceCount", base.bounce_count);
    read_setting(object, "spatialResampleVisibilityTestMaxDepth",
                 base.spatial_visibility_max_depth);
    read_setting(object, "denoiseThresh", base.denoise_threshold);
    read_setting(object, "globalIllumMaxAccum", base.global_illumination_max_accumulation);
    read_setting(object, "radiusLitFactor", base.radius_lit_factor);
    read_setting(object, "spatialResampleSize", base.spatial_resample_size);
    read_setting(object, "noiseSupressionFactor", base.noise_suppression_factor);
}
json::object write_base(const settings::Base& base) {
    return {{"isDenoise", base.denoise},
            {"isTAAJitter", base.taa_jitter},
            {"skipSamples", base.skip_samples},
            {"isSampleLeveling", base.sample_leveling},
            {"isVaryingResmaplingRadius", base.varying_resampling_radius},
            {"isAtmosphereInteraction", base.atmosphere_interaction},
            {"taaSampleMaxAge", base.taa_sample_max_age},
            {"bounceCount", base.bounce_count},
            {"spatialResampleVisibilityTestMaxDepth", base.spatial_visibility_max_depth},
            {"denoiseThresh", base.denoise_threshold},
            {"globalIllumMaxAccum", base.global_illumination_max_accumulation},
            {"radiusLitFactor", base.radius_lit_factor},
            {"spatialResampleSize", base.spatial_resample_size},
            {"noiseSupressionFactor", base.noise_suppression_factor}};
}
} // namespace
void read_render(const json::object& object, settings::Render& render) {
    read_setting(object, "showSteps", render.show_steps);
    auto version = static_cast<int>(render.version);
    read_setting(object, "version", version);
    if (version < 0 || version > 2) {
        throw std::runtime_error("Unknown render version");
    }
    render.version = static_cast<settings::RenderVersion>(version);
    const auto& albedo = object_setting(object, "renderAlbedo");
    read_setting(albedo, "isTAAJitter", render.albedo.taa_jitter);
    read_setting(albedo, "checkSun", render.albedo.check_sun);
    read_setting(albedo, "taaSampleMaxAge", render.albedo.taa_sample_max_age);
    read_base(object_setting(object, "renderBase"), render.base);
    const auto& path = object_setting(object, "renderPathTracer");
    read_setting(path, "isTAAJitter", render.path_tracer.taa_jitter);
    read_setting(path, "isAtmosphereInteraction", render.path_tracer.atmosphere_interaction);
    read_setting(path, "maxSamples", render.path_tracer.max_samples);
    read_setting(path, "bounceCount", render.path_tracer.bounce_count);
}
json::object write_render(const settings::Render& render) {
    json::object albedo{{"isTAAJitter", render.albedo.taa_jitter},
                        {"checkSun", render.albedo.check_sun},
                        {"taaSampleMaxAge", render.albedo.taa_sample_max_age}};
    json::object path{{"isTAAJitter", render.path_tracer.taa_jitter},
                      {"isAtmosphereInteraction", render.path_tracer.atmosphere_interaction},
                      {"maxSamples", render.path_tracer.max_samples},
                      {"bounceCount", render.path_tracer.bounce_count}};
    return {{"showSteps", render.show_steps},
            {"version", static_cast<int>(render.version)},
            {"renderAlbedo", std::move(albedo)},
            {"renderBase", write_base(render.base)},
            {"renderPathTracer", std::move(path)}};
}
} // namespace engine::rendering::configuration
