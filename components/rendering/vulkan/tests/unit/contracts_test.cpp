#include "../../src/state.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

foundation::geometry::Vec3 unproject(const Matrix& inverse, float x, float y) {
    const std::array<float, 4> clip{x, y, 1, 1};
    std::array<float, 4> output{};
    for (unsigned column = 0; column < 4; ++column) {
        for (unsigned row = 0; row < 4; ++row) {
            output[column] += clip[row] * inverse[row * 4 + column];
        }
    }
    return foundation::geometry::normalized({output[0], output[1], output[2]});
}

void camera_contract() {
    auto camera = motion::camera::Camera::create(1024, {{64, 80, 120}, 0.43, -0.26});
    require(bool(camera), "Camera creation failed");
    const auto configured = camera->configure({2, 24, 90});
    require(bool(configured), "Camera configuration failed");
    const auto inverse = inverse_matrix(view_projection(*camera, 1600.0f / 900.0f));
    const std::array pixels{motion::camera::Pixel{0, 0}, motion::camera::Pixel{799, 449},
                            motion::camera::Pixel{1599, 899}, motion::camera::Pixel{200, 700}};
    for (const auto pixel : pixels) {
        const auto expected = camera->direction({pixel.x + 0.5, pixel.y + 0.5}, {1600, 900});
        require(bool(expected), "Camera ray creation failed");
        const auto actual = unproject(inverse, float((pixel.x + 0.5) / 800.0 - 1),
                                      float(1 - (pixel.y + 0.5) / 450.0));
        require(foundation::geometry::length(actual - *expected) < 0.00001,
                "Slang/XNA row-major ray does not match the engine camera");
    }
}

void atmosphere_contract() {
    settings::Atmosphere sky;
    for (const auto angle : {0.0f, 60.0f, 90.0f, 120.0f, 180.0f}) {
        sky.sun_angle = angle;
        const auto light = sunlight(sky);
        for (const auto channel : light) {
            require(std::isfinite(channel) && channel >= 0 &&
                        channel <= sky.sun_intensity + 0.0001f,
                    "Atmosphere transport must attenuate finite incident sunlight");
        }
    }
}

void png_contract(const std::filesystem::path& path) {
    const std::array<std::byte, 16> colors{
        std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
        std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255},
        std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    write_png(path, {2, 2}, colors, false);
    require(std::filesystem::file_size(path) > 50, "PNG capture is empty");
}

void write_reflection(const std::filesystem::path& path, std::uint32_t offset, std::uint32_t size) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << R"({"parameters":[{"name":"value","type":{"kind":"scalar"},)"
           << R"("binding":{"kind":"uniform","offset":)" << offset << R"(,"size":)" << size
           << R"(}}],"entryPoints":[{"name":"test","stage":"compute"}]})";
    require(bool(stream), "Could not write reflection contract fixture");
}

void reflection_contract(std::filesystem::path path) {
    path.replace_extension(".reflection.json");
    write_reflection(path, 12, 12);
    const auto valid = read_reflection(path);
    require(valid.uniform_size == 32 && valid.uniforms.at("value").offset == 12,
            "Valid reflected uniform lost its offset or alignment");
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    const std::array ranges{std::array<std::uint32_t, 2>{maximum - 3, 8},
                            std::array<std::uint32_t, 2>{maximum - 16, 2}};
    for (const auto& range : ranges) {
        write_reflection(path, range[0], range[1]);
        bool rejected = false;
        try {
            static_cast<void>(read_reflection(path));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Malformed reflected uniform range wrapped its allocation size");
    }
    std::filesystem::remove(path);
}
} // namespace
} // namespace engine::rendering::vulkan

int main(int argc, char** argv) {
    try {
        engine::rendering::vulkan::camera_contract();
        engine::rendering::vulkan::atmosphere_contract();
        if (argc == 2) {
            engine::rendering::vulkan::png_contract(argv[1]);
            engine::rendering::vulkan::reflection_contract(argv[1]);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
