#include <engine/rendering/configuration/configuration.hpp>
#include <engine/rendering/settings/settings.hpp>
#include <iostream>
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
