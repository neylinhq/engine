#include "import.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <unordered_map>

namespace engine::world::storage {
namespace {

model::Vec3 rgb(std::uint32_t color) {
    return {static_cast<double>((color >> 16) & 255U), static_cast<double>((color >> 8) & 255U),
            static_cast<double>(color & 255U)};
}

double distance(model::Vec3 left, model::Vec3 right, bool training) {
    const auto difference = left - right;
    const auto red = difference.x * difference.x;
    const auto green = difference.y * difference.y;
    const auto blue = difference.z * difference.z;
    return training ? red + green + blue : std::max({red, green, blue});
}

std::size_t nearest(model::Vec3 color, std::span<const model::Vec3> palette,
                    bool training = false) {
    auto best_distance = std::numeric_limits<double>::max();
    std::size_t best = 0;
    for (std::size_t index = 0; index < palette.size(); ++index) {
        const auto candidate = distance(color, palette[index], training);
        if (candidate < best_distance) {
            best = index;
            best_distance = candidate;
        }
    }
    return best;
}

std::size_t weighted_sample(std::span<const double> weights, std::mt19937& random) {
    const auto total = std::accumulate(weights.begin(), weights.end(), 0.0);
    auto remaining = std::generate_canonical<double, 53>(random) * total;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        remaining -= weights[index];
        if (remaining < 0) {
            return index;
        }
    }
    return weights.size() - 1;
}

std::vector<model::Vec3> seed_palette(std::span<const model::Vec3> colors, std::size_t count,
                                      std::mt19937& random) {
    std::vector<model::Vec3> palette;
    palette.reserve(count);
    std::uniform_int_distribution<std::size_t> first(0, colors.size() - 1);
    palette.push_back(colors[first(random)]);
    std::vector<double> weights(colors.size(), std::numeric_limits<double>::max());
    while (palette.size() < count) {
        for (std::size_t index = 0; index < colors.size(); ++index) {
            weights[index] =
                std::min(weights[index], distance(colors[index], palette.back(), true));
        }
        palette.push_back(colors[weighted_sample(weights, random)]);
    }
    return palette;
}

bool coordinate_converged(double previous, double next) {
    if (previous == 0) {
        return next == 0;
    }
    return std::abs((previous - next) / previous) < 0.1;
}

bool converged(std::span<const model::Vec3> previous, std::span<const model::Vec3> next) {
    for (std::size_t index = 0; index < previous.size(); ++index) {
        if (!coordinate_converged(previous[index].x, next[index].x) ||
            !coordinate_converged(previous[index].y, next[index].y) ||
            !coordinate_converged(previous[index].z, next[index].z)) {
            return false;
        }
    }
    return true;
}

void lloyd_step(std::span<const model::Vec3> colors, std::vector<model::Vec3>& palette) {
    std::vector<model::Vec3> sums(palette.size());
    std::vector<std::size_t> weights(palette.size());
    for (const auto color : colors) {
        const auto slot = nearest(color, palette, true);
        sums[slot] = sums[slot] + color;
        ++weights[slot];
    }
    for (std::size_t index = 0; index < palette.size(); ++index) {
        if (weights[index] != 0) {
            palette[index] = sums[index] / static_cast<double>(weights[index]);
        }
    }
}

void shuffle_batch(std::vector<std::size_t>& indices, std::size_t count, std::mt19937& random) {
    for (std::size_t index = 0; index < count; ++index) {
        std::uniform_int_distribution<std::size_t> select(index, indices.size() - 1);
        std::swap(indices[index], indices[select(random)]);
    }
}

void minibatch_step(std::span<const model::Vec3> colors, std::span<const std::size_t> batch,
                    std::vector<model::Vec3>& palette, std::vector<std::uint64_t>& counts) {
    std::vector<std::size_t> assignments;
    assignments.reserve(batch.size());
    for (const auto index : batch) {
        assignments.push_back(nearest(colors[index], palette, true));
    }
    for (std::size_t sample = 0; sample < batch.size(); ++sample) {
        const auto slot = assignments[sample];
        const auto rate = 1.0 / static_cast<double>(++counts[slot]);
        palette[slot] = palette[slot] + (colors[batch[sample]] - palette[slot]) * rate;
    }
}

void train_minibatches(std::span<const model::Vec3> colors, std::vector<model::Vec3>& palette,
                       std::mt19937& random) {
    constexpr std::size_t batch_size = 50000;
    std::vector<std::size_t> indices(colors.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::vector<std::uint64_t> counts(palette.size());
    for (;;) {
        const auto previous = palette;
        shuffle_batch(indices, batch_size, random);
        minibatch_step(colors, std::span(indices).first(batch_size), palette, counts);
        if (converged(previous, palette)) {
            return;
        }
    }
}

std::vector<model::Vec3> quantize(std::span<const model::Vec3> colors, std::size_t count) {
    // Accord uses random K-means++ initialization. A fixed seed makes imports reproducible.
    std::mt19937 random(0x4e414144U);
    auto palette = seed_palette(colors, count, random);
    if (colors.size() > 50000) {
        train_minibatches(colors, palette, random);
    } else {
        for (;;) {
            const auto previous = palette;
            lloyd_step(colors, palette);
            if (converged(previous, palette)) {
                break;
            }
        }
    }
    for (auto& color : palette) {
        color = {std::trunc(color.x), std::trunc(color.y), std::trunc(color.z)};
    }
    return palette;
}

model::Material material(model::Vec3 color) {
    model::Material result;
    result.color_base = {std::pow(color.x / 255, 2.2), std::pow(color.y / 255, 2.2),
                         std::pow(color.z / 255, 2.2)};
    return result;
}

} // namespace

model::Model colored_model(model::IVec3 size, std::span<const ColorVoxel> voxels,
                           int palette_size) {
    palette_size = std::clamp(palette_size, 1, 32767);
    std::map<std::uint32_t, model::Vec3> unique;
    for (const auto& voxel : voxels) {
        unique.emplace(voxel.color, rgb(voxel.color));
    }
    std::vector<model::Vec3> colors;
    for (const auto& [key, color] : unique) {
        static_cast<void>(key);
        colors.push_back(color);
    }
    auto palette = colors;
    if (palette.size() > static_cast<std::size_t>(palette_size)) {
        palette = quantize(colors, static_cast<std::size_t>(palette_size));
    }
    std::vector<model::Material> materials(1);
    for (auto color : palette) {
        materials.push_back(material(color));
    }
    std::unordered_map<std::uint32_t, std::uint16_t> mapping;
    for (const auto& [key, color] : unique) {
        mapping.emplace(key, static_cast<std::uint16_t>(nearest(color, palette) + 1));
    }
    std::vector<model::Sample> samples;
    samples.reserve(voxels.size());
    for (const auto& voxel : voxels) {
        samples.push_back({voxel.position, mapping.at(voxel.color)});
    }
    return model::build_model(size, std::move(materials), samples);
}

} // namespace engine::world::storage
