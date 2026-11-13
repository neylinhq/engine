#include <engine/rendering/configuration/configuration.hpp>

#include "settings.hpp"

#include <engine/platform/filesystem/filesystem.hpp>

#include <span>
#include <stdexcept>

namespace engine::rendering::configuration {
namespace {

constexpr std::size_t kMaxSettingsBytes = 16 * 1024 * 1024;

} // namespace

settings::Settings load(const std::filesystem::path& path) {
    const auto bytes = platform::filesystem::read(path, kMaxSettingsBytes);
    try {
        const std::string_view text =
            bytes.empty()
                ? std::string_view{}
                : std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return parse(text, true);
    } catch (const std::exception& error) {
        const auto name = path.u8string();
        throw std::runtime_error(
            "Invalid settings " +
            std::string(reinterpret_cast<const char*>(name.data()), name.size()) + ": " +
            error.what());
    }
}

void save(const std::filesystem::path& path, const settings::Settings& settings) {
    const auto text = serialize(checked_settings(settings, true)) + '\n';
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    platform::filesystem::write(path, std::as_bytes(std::span(text)));
}

} // namespace engine::rendering::configuration
