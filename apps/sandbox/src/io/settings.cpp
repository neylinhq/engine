#include "../application.hpp"
#include <engine/rendering/configuration/configuration.hpp>
namespace engine::app::sandbox {
void load_settings(Application& app) {
    if (!app.options.settings.empty()) {
        app.settings = rendering::configuration::load(app.options.settings);
    }
    if (app.options.mode) {
        app.settings.render.version = *app.options.mode;
    }
}
void save_settings(const Application& app, const std::filesystem::path& path) {
    rendering::configuration::save(path, app.settings);
}
} // namespace engine::app::sandbox
