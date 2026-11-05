#include "settings.hpp"
namespace engine::rendering::configuration {
namespace {
void read_array(const json::object& object, std::string_view key, std::array<float, 3>& values) {
    const auto* value = object.if_contains(key);
    if (!value) {
        return;
    }
    const auto& array = value->as_array();
    if (array.size() != 3) {
        throw std::runtime_error("Sky vector must have three elements");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = array[index].to_number<float>();
        if (!std::isfinite(values[index])) {
            throw std::runtime_error("Sky vector must be finite");
        }
    }
}
json::array write_array(const std::array<float, 3>& values) {
    return {values[0], values[1], values[2]};
}
} // namespace
void read_atmosphere(const json::object& object, settings::Atmosphere& sky) {
    read_setting(object, "sunAngle", sky.sun_angle);
    read_setting(object, "sunAzimuth", sky.sun_azimuth);
    read_setting(object, "sunIntensity", sky.sun_intensity);
    read_array(object, "sunColor", sky.sun_color);
    read_array(object, "rayleighScatter", sky.rayleigh_scatter);
    read_setting(object, "mieScatter", sky.mie_scatter);
    read_array(object, "ozoneAbsorb", sky.ozone_absorb);
    read_setting(object, "sphereRadius", sky.sphere_radius);
    read_setting(object, "thickness", sky.thickness);
    read_setting(object, "averagePoint", sky.average_point);
    read_setting(object, "density", sky.density);
    read_setting(object, "absorbIntensity", sky.absorb_intensity);
    read_setting(object, "scatterIntensity", sky.scatter_intensity);
    read_setting(object, "mieFactor", sky.mie_factor);
    read_setting(object, "mainRaySteps", sky.main_ray_steps);
    read_setting(object, "subScatterSteps", sky.sub_scatter_steps);
    if (sky.sphere_radius <= 0 || sky.thickness <= 0 || sky.average_point <= 0 ||
        sky.main_ray_steps < 1 || sky.main_ray_steps > 100 || sky.sub_scatter_steps < 1 ||
        sky.sub_scatter_steps > 100 || sky.mie_factor < -1 || sky.mie_factor >= 1) {
        throw std::runtime_error("Atmosphere settings outside supported bounds");
    }
}
json::object write_atmosphere(const settings::Atmosphere& sky) {
    return {{"sunAngle", sky.sun_angle},
            {"sunAzimuth", sky.sun_azimuth},
            {"sunIntensity", sky.sun_intensity},
            {"sunColor", write_array(sky.sun_color)},
            {"rayleighScatter", write_array(sky.rayleigh_scatter)},
            {"mieScatter", sky.mie_scatter},
            {"ozoneAbsorb", write_array(sky.ozone_absorb)},
            {"sphereRadius", sky.sphere_radius},
            {"thickness", sky.thickness},
            {"averagePoint", sky.average_point},
            {"density", sky.density},
            {"absorbIntensity", sky.absorb_intensity},
            {"scatterIntensity", sky.scatter_intensity},
            {"mieFactor", sky.mie_factor},
            {"mainRaySteps", sky.main_ray_steps},
            {"subScatterSteps", sky.sub_scatter_steps}};
}
} // namespace engine::rendering::configuration
