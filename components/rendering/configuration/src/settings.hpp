#ifndef ENGINE_RENDERING_CONFIGURATION_SRC_SETTINGS_HPP
#define ENGINE_RENDERING_CONFIGURATION_SRC_SETTINGS_HPP
#include <boost/json.hpp>
#include <cmath>
#include <engine/rendering/settings/settings.hpp>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
namespace engine::rendering::configuration {
namespace json = boost::json;
template <typename Value>
void read_setting(const json::object& object, std::string_view key, Value& target) {
    const auto* value = object.if_contains(key);
    if (!value) {
        return;
    }
    if constexpr (std::is_same_v<Value, bool>) {
        target = value->as_bool();
    } else {
        const auto numeric = value->to_number<double>();
        if (!std::isfinite(numeric)) {
            throw std::runtime_error("Setting must be finite: " + std::string(key));
        }
        if constexpr (std::is_integral_v<Value>) {
            if (std::trunc(numeric) != numeric) {
                throw std::runtime_error("Setting must be an integer: " + std::string(key));
            }
        }
        if (numeric < static_cast<double>(std::numeric_limits<Value>::lowest()) ||
            numeric > static_cast<double>(std::numeric_limits<Value>::max())) {
            throw std::runtime_error("Setting is out of range: " + std::string(key));
        }
        target = static_cast<Value>(numeric);
    }
}
void read_render(const json::object& object, settings::Render& render);
void read_atmosphere(const json::object& object, settings::Atmosphere& sky);
[[nodiscard]] json::object write_render(const settings::Render& render);
[[nodiscard]] json::object write_atmosphere(const settings::Atmosphere& sky);
[[nodiscard]] settings::Settings checked_settings(const settings::Settings& settings, bool strict);
void validate_atmosphere(const settings::Atmosphere& sky);
} // namespace engine::rendering::configuration
#endif
