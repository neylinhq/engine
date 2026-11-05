#include "values.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::rendering::vulkan {
namespace {
Vector3 optical_densities(const settings::Atmosphere& sky, Vector3 direction) {
    const float origin_y = sky.sphere_radius + 10.0f;
    const float outer = sky.sphere_radius + sky.thickness;
    const double half_b = double(origin_y) * direction[1];
    const double c = double(origin_y) * origin_y - double(outer) * outer;
    const double discriminant = half_b * half_b - c;
    if (discriminant <= 0) {
        return {};
    }
    const double near = std::max(0.0, -half_b - std::sqrt(discriminant));
    const double far = -half_b + std::sqrt(discriminant);
    const auto length = std::max(0.0, far - near);
    Vector3 density{};
    for (int index = 19; index >= 0; --index) {
        const auto distance = near + length * index / 20.0;
        const auto x = direction[0] * distance;
        const auto y = origin_y + direction[1] * distance;
        const auto z = direction[2] * distance;
        const auto height =
            std::max(0.0, std::sqrt(x * x + y * y + z * z) - sky.sphere_radius) / sky.thickness;
        const auto scale = float(length / 20.0) * sky.density * 0.01f;
        density[0] += float(std::exp(-height / 0.3) * (1 - height)) * scale;
        density[1] += float(std::exp(-height / 0.2) * (1 - height)) * scale;
        density[2] += float(std::max(0.0, 1 - std::abs(height - 0.25) / 0.15)) * scale;
    }
    return density;
}
} // namespace

Vector3 sun_direction(const settings::Atmosphere& sky) {
    const auto elevation = sky.sun_angle * std::numbers::pi_v<float> / 180.0f;
    const auto azimuth = sky.sun_azimuth * std::numbers::pi_v<float> / 180.0f;
    return {-std::sin(elevation) * std::cos(azimuth), std::cos(elevation),
            std::sin(elevation) * std::sin(azimuth)};
}

Vector3 sunlight(const settings::Atmosphere& sky) {
    const auto density = optical_densities(sky, sun_direction(sky));
    Vector3 result{};
    for (unsigned channel = 0; channel < 3; ++channel) {
        const auto scatter = density[0] * sky.rayleigh_scatter[channel] +
                             density[1] * sky.mie_scatter + density[2] * sky.ozone_absorb[channel];
        result[channel] =
            std::exp(-scatter * sky.scatter_intensity * 0.000001f * sky.absorb_intensity) *
            sky.sun_color[channel] * sky.sun_intensity;
    }
    return result;
}

void atmosphere_parameters(UniformValues& values, const settings::Atmosphere& sky) {
    set_value(values, "skySunDir", sun_direction(sky));
    set_value(values, "skyRayleighScatter", sky.rayleigh_scatter);
    set_value(values, "skyMieScatter", sky.mie_scatter);
    set_value(values, "skyOzoneAbsorb", sky.ozone_absorb);
    const Vector3 sun{sky.sun_color[0] * sky.sun_intensity, sky.sun_color[1] * sky.sun_intensity,
                      sky.sun_color[2] * sky.sun_intensity};
    set_value(values, "skySunColor", sun);
    set_value(values, "skySphereRadius", sky.sphere_radius);
    set_value(values, "skyAtmosphereThickness", sky.thickness);
    set_value(values, "skyAtmosphereAveragePoint", sky.average_point);
    set_value(values, "skyAtmosphereDensity", sky.density * 0.01f);
    set_value(values, "skyAbsorbIntensity", sky.absorb_intensity);
    set_value(values, "skyScatterIntensity", sky.scatter_intensity * 0.000001f);
    set_value(values, "skyMieFactor", sky.mie_factor);
    set_value(values, "skyMainRaySteps", sky.main_ray_steps);
    set_value(values, "skySubScatterSteps", sky.sub_scatter_steps);
    set_value(values, "sunColor", sunlight(sky));
}

} // namespace engine::rendering::vulkan
