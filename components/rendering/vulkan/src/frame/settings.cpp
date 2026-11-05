#include "values.hpp"

#include <algorithm>

namespace engine::rendering::vulkan {

void render_parameters(UniformValues& values, const settings::Settings& settings) {
    const auto& base = settings.render.base;
    const bool path = settings.render.version == settings::RenderVersion::path_tracer;
    const bool albedo = settings.render.version == settings::RenderVersion::albedo;
    const auto sample_age =
        albedo ? settings.render.albedo.taa_sample_max_age : base.taa_sample_max_age;
    set_value(values, "exposure", settings.general.exposure);
    set_value(values, "toneMappingFac", settings.general.tone_mapping);
    set_boolean(values, "showRayStep", settings.render.show_steps);
    set_boolean(values, "checkSun", settings.render.albedo.check_sun);
    set_boolean(values, "isTAA", sample_age > 1 && !settings.render.show_steps);
    set_value(values, "sampleAge", std::clamp(sample_age, 1, 32));
    set_value(values, "maxBounceCount",
              path ? settings.render.path_tracer.bounce_count : base.bounce_count);
    set_value(values, "sampleMaxAccum",
              std::clamp(base.global_illumination_max_accumulation, 1, 128));
    set_value(values, "validSampleStorageCount", 2);
    set_value(values, "invalidSampleStorageCount", 8);
    set_value(values, "bucketStorageCount", 32);
    set_value(values, "refinedBucketStorageCount", 8);
    set_boolean(values, "skipSamples", base.skip_samples);
    set_boolean(values, "isSampleLeveling", base.sample_leveling);
    set_boolean(values, "isVaryingResmaplingRadius", base.varying_resampling_radius);
    set_boolean(values, "isDenoise", base.denoise);
    set_boolean(values, "isAtmosphereInteraction",
                path ? settings.render.path_tracer.atmosphere_interaction
                     : base.atmosphere_interaction);
    set_value(values, "noiseSupressionFactor", base.noise_suppression_factor);
    set_value(values, "test1", base.spatial_resample_size);
    set_value(values, "radiusLitFactor", base.radius_lit_factor);
    set_value(values, "spatialVisibilityCount", base.spatial_visibility_max_depth);
    set_value(values, "denoiseThresh", base.denoise_threshold);
}

} // namespace engine::rendering::vulkan
