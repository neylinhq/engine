#include <chrono>
#include <engine/rendering/configuration/configuration.hpp>
#include <engine/rendering/settings/settings.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool value) {
    if (!value) {
        throw std::runtime_error("Settings contract failed");
    }
}
void rejected(std::string_view text) {
    bool failed = false;
    try {
        static_cast<void>(engine::rendering::configuration::parse(text, true));
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed);
}
void reject_save(const std::filesystem::path& path,
                 const engine::rendering::settings::Settings& settings) {
    bool rejected = false;
    try {
        engine::rendering::configuration::save(path, settings);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected);
}
void file_round_trip() {
    using namespace engine::rendering;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("engine-" + std::to_string(stamp) + ".settings.json");
    settings::Settings original;
    original.general.fov = 70;
    configuration::save(path, original);
    auto updated = original;
    updated.general.fov = 65;
    configuration::save(path, updated);
    require(configuration::load(path) == updated);
    auto invalid = updated;
    invalid.render.base.bounce_count = 11;
    reject_save(path, invalid);
    require(configuration::load(path) == updated);
    invalid = updated;
    invalid.atmosphere.sun_color[1] = std::numeric_limits<float>::infinity();
    reject_save(path, invalid);
    require(configuration::load(path) == updated);
    invalid = updated;
    invalid.atmosphere.mie_factor = 1;
    reject_save(path, invalid);
    require(configuration::load(path) == updated);
    invalid = updated;
    invalid.render.version = static_cast<settings::RenderVersion>(3);
    reject_save(path, invalid);
    require(configuration::load(path) == updated);
    std::filesystem::remove(path);
}
} // namespace
int main() {
    try {
        using namespace engine::rendering;
        settings::Settings original;
        original.general.fov = 70;
        original.render.base.bounce_count = 7;
        original.atmosphere.sun_azimuth = 123;
        require(configuration::parse(configuration::serialize(original), true) == original);
        rejected(R"({"general":{"fvo":70}})");
        rejected(R"({"general":{"fov":500}})");
        rejected(R"({"render":{"renderBase":{"bounceCount":1.5}}})");
        rejected(R"({"atmosphere":{"mieFactor":1}})");
        require(configuration::parse(R"({"general":{"fov":500}})").general.fov == 120);
        require(configuration::parse(R"({"general":{"fov":70}})", true).general.fov == 70);
        file_round_trip();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
