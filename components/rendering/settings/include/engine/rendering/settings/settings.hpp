#ifndef ENGINE_RENDERING_SETTINGS_SETTINGS_HPP
#define ENGINE_RENDERING_SETTINGS_SETTINGS_HPP

#include <array>

namespace engine::rendering::settings {

enum class RenderVersion { albedo, base, path_tracer };

struct General {
    float exposure = 1.0f;
    float tone_mapping = 1.5f;
    float fov = 90.0f;
    bool lock_to_60 = false;
    bool operator==(const General&) const = default;
};

struct Albedo {
    bool taa_jitter = false;
    bool check_sun = false;
    int taa_sample_max_age = 1;
    bool operator==(const Albedo&) const = default;
};

struct Base {
    bool denoise = true;
    bool taa_jitter = true;
    bool skip_samples = true;
    bool sample_leveling = true;
    bool varying_resampling_radius = true;
    bool atmosphere_interaction = true;
    int taa_sample_max_age = 32;
    int bounce_count = 3;
    int spatial_visibility_max_depth = 80;
    float denoise_threshold = 400.0f;
    int global_illumination_max_accumulation = 128;
    float radius_lit_factor = 3.0f;
    float spatial_resample_size = 500.0f;
    float noise_suppression_factor = 0.4f;
    bool operator==(const Base&) const = default;
};

struct PathTracer {
    bool taa_jitter = true;
    bool atmosphere_interaction = true;
    int max_samples = 8192;
    int bounce_count = 3;
    bool operator==(const PathTracer&) const = default;
};

struct Render {
    bool show_steps = false;
    RenderVersion version = RenderVersion::base;
    Albedo albedo;
    Base base;
    PathTracer path_tracer;
    bool operator==(const Render&) const = default;
};

struct Atmosphere {
    float sun_angle = 60.0f;
    float sun_azimuth = 30.0f;
    float sun_intensity = 10.0f;
    std::array<float, 3> rayleigh_scatter{5.802f, 13.558f, 33.1f};
    float mie_scatter = 2.5f;
    std::array<float, 3> ozone_absorb{0.650f, 1.881f, 0.085f};
    std::array<float, 3> sun_color{1.0f, 1.0f, 1.0f};
    float sphere_radius = 5000000.0f;
    float thickness = 50000.0f;
    float average_point = 0.08f;
    float density = 14.0f;
    float absorb_intensity = 3.0f;
    float scatter_intensity = 1.35f;
    float mie_factor = 0.85f;
    int main_ray_steps = 24;
    int sub_scatter_steps = 6;
    bool operator==(const Atmosphere&) const = default;
};

struct Settings {
    General general;
    Render render;
    Atmosphere atmosphere;
    bool operator==(const Settings&) const = default;
};

} // namespace engine::rendering::settings
#endif
