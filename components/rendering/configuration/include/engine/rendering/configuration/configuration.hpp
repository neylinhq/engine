#ifndef ENGINE_RENDERING_CONFIGURATION_CONFIGURATION_HPP
#define ENGINE_RENDERING_CONFIGURATION_CONFIGURATION_HPP
#include <engine/rendering/settings/settings.hpp>
#include <string>
#include <string_view>
namespace engine::rendering::configuration {
[[nodiscard]] settings::Settings parse(std::string_view text, bool strict = false);
[[nodiscard]] std::string serialize(const settings::Settings& settings);
} // namespace engine::rendering::configuration
#endif
