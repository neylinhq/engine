#include "../../src/import.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using engine::world::storage::colored_model;
using engine::world::storage::ColorVoxel;

double srgb_channel(double linear) {
    return std::pow(linear, 1.0 / 2.2) * 255;
}

void require(bool condition, const char* description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}

void integer_centroid() {
    const std::vector<ColorVoxel> voxels{{{0, 0, 0}, 0}, {{1, 0, 0}, 2}, {{2, 0, 0}, 3}};
    const auto model = colored_model({3, 1, 1}, voxels, 1);
    require(model.materials.size() == 2, "Palette must retain reserved material zero");
    require(std::abs(srgb_channel(model.materials[1].color_base.z) - 1) < 1e-9,
            "Original palette converts centroid channels to integer before linearization");
}

void separated_clusters() {
    const std::vector<ColorVoxel> voxels{
        {{0, 0, 0}, 0x000000}, {{1, 0, 0}, 0x00001e}, {{2, 0, 0}, 0x1e1e00}, {{3, 0, 0}, 0xffffff}};
    const auto model = colored_model({4, 1, 1}, voxels, 2);
    require(model.materials.size() == 3, "Two requested clusters must produce two materials");
    std::vector<double> channels;
    for (std::size_t index = 1; index < model.materials.size(); ++index) {
        const auto color = model.materials[index].color_base;
        require(std::abs(color.x - color.y) < 1e-12 && std::abs(color.y - color.z) < 1e-12,
                "Known separated cluster centroid must have equal RGB channels");
        channels.push_back(srgb_channel(color.x));
    }
    std::ranges::sort(channels);
    require(std::abs(channels[0] - 10) < 1e-9 && std::abs(channels[1] - 255) < 1e-9,
            "Squared-Euclidean cluster means disagree with independent expected centroids");
}

void large_color_set() {
    std::vector<ColorVoxel> voxels;
    voxels.reserve(65536);
    for (int red = 0; red < 256; ++red) {
        for (int green = 0; green < 256; ++green) {
            const auto color = static_cast<std::uint32_t>((red << 16) | (green << 8) | 16);
            voxels.push_back({{red, 0, green}, color});
        }
    }
    const auto model = colored_model({256, 1, 256}, voxels, 1);
    const auto color = model.materials.at(1).color_base;
    require(std::abs(srgb_channel(color.x) - 127.5) <= 2 &&
                std::abs(srgb_channel(color.y) - 127.5) <= 2,
            "Large-set minibatch mean must approximate uniform red/green distribution");
    require(std::abs(srgb_channel(color.z) - 16) < 1e-9,
            "Minibatch update must retain a constant blue component");
}

} // namespace

int main() {
    try {
        integer_centroid();
        separated_clusters();
        large_color_set();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
