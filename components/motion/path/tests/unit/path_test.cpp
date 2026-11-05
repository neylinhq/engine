#include <cmath>
#include <engine/motion/path/path.hpp>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void interpolation() {
    engine::motion::path::Path path;
    require(
        path.replace({{{0, 0, 0}, {0, 0, 1}, 1, "a"}, {{3, 0, 0}, {0, 0, 1}, 1, "b"}}).has_value(),
        "valid nodes");
    path.start();
    auto sample = path.advance(0.5, true);
    require(sample.position.x == 0, "linear start");
    sample = path.advance(0.5, true);
    require(std::abs(sample.position.x - 1.5) < 1e-12, "linear middle");
    sample = path.advance(0, true);
    require(sample.position.x == 3 && sample.final_frame && !path.playing(), "linear end");
    path.start();
    sample = path.advance(0, false);
    require(std::abs(sample.position.x - 1) < 1e-12, "original smooth endpoint weights");
}
void boundaries() {
    engine::motion::path::Path path;
    path.start();
    require(!path.playing(), "empty playback");
    require(!path.insert(0, {{}, {}, 1, "bad"}), "zero direction rejected");
    require(path.insert(0, {{1, 2, 3}, {0, 1, 0}, 1, "only"}).has_value(), "single node");
    path.start();
    const auto sample = path.advance(0.1, false);
    require(sample.final_frame && sample.position.x == 1, "single final frame");
}
} // namespace
int main() {
    try {
        interpolation();
        boundaries();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
