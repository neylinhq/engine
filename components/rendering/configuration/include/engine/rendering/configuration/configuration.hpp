#ifndef ENGINE_RENDERING_CONFIGURATION_CONFIGURATION_HPP
#define ENGINE_RENDERING_CONFIGURATION_CONFIGURATION_HPP
#include <engine/rendering/settings/settings.hpp>
#include <filesystem>
#include <string>
#include <string_view>
namespace engine::rendering::configuration {
[[nodiscard]] settings::Settings parse(std::string_view text, bool strict = false);
// An explicit settings file uses the strict decoder.
[[nodiscard]] settings::Settings load(const std::filesystem::path& path);
// Validate settings before writing or replacing the file.
void save(const std::filesystem::path& path, const settings::Settings& settings);
[[nodiscard]] std::string serialize(const settings::Settings& settings);
} // namespace engine::rendering::configuration
#endif
