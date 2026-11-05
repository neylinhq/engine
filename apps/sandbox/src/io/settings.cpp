#include "../application.hpp"
#include "path.hpp"
#include <engine/rendering/configuration/configuration.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
namespace engine::app::sandbox {
void load_settings(Application& app) {
    if (!std::filesystem::exists(app.options.settings)) {
        return;
    }
    try {
        std::ifstream stream(app.options.settings, std::ios::binary);
        std::ostringstream text;
        text << stream.rdbuf();
        app.settings = rendering::configuration::parse(text.str());
    } catch (const std::exception& error) {
        std::cerr << "Settings ignored: " << error.what() << '\n';
    }
}
void save_settings(const Application& app) {
    if (!app.options.settings.parent_path().empty()) {
        std::filesystem::create_directories(app.options.settings.parent_path());
    }
    std::ofstream stream(app.options.settings, std::ios::binary | std::ios::trunc);
    stream << rendering::configuration::serialize(app.settings) << '\n';
    if (!stream) {
        throw std::runtime_error("Could not save settings: " + path_to_utf8(app.options.settings));
    }
}
} // namespace engine::app::sandbox
