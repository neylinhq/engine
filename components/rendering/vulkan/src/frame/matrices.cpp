#include "values.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
Matrix multiply(const Matrix& left, const Matrix& right) {
    Matrix result{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            for (unsigned inner = 0; inner < 4; ++inner) {
                result[row * 4 + column] += left[row * 4 + inner] * right[inner * 4 + column];
            }
        }
    }
    return result;
}

float halton(std::uint32_t index, std::uint32_t base) {
    float fraction = 1.0f;
    float result = 0.0f;
    while (index) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

void eliminate(std::array<std::array<double, 8>, 4>& rows, unsigned pivot) {
    auto selected = pivot;
    for (unsigned row = pivot + 1; row < 4; ++row) {
        if (std::abs(rows[row][pivot]) > std::abs(rows[selected][pivot])) {
            selected = row;
        }
    }
    std::swap(rows[pivot], rows[selected]);
    const auto divisor = rows[pivot][pivot];
    if (std::abs(divisor) < 1e-15) {
        throw std::runtime_error("Singular camera matrix");
    }
    for (auto& value : rows[pivot]) {
        value /= divisor;
    }
    for (unsigned row = 0; row < 4; ++row) {
        if (row == pivot) {
            continue;
        }
        const auto factor = rows[row][pivot];
        for (unsigned column = 0; column < 8; ++column) {
            rows[row][column] -= factor * rows[pivot][column];
        }
    }
}
} // namespace

Matrix view_projection(const motion::camera::Camera& camera, float aspect) {
    const auto basis = camera.basis();
    const auto& r = basis.right;
    const auto& u = basis.up;
    const auto& f = basis.forward;
    const Matrix view{float(r.x),  float(u.x), float(-f.x), 0,          float(r.y),  float(u.y),
                      float(-f.y), 0,          float(r.z),  float(u.z), float(-f.z), 0,
                      0,           0,          0,           1};
    const auto scale =
        1.0f / std::tan(float(camera.settings().fov_degrees) * std::numbers::pi_v<float> / 360.0f);
    constexpr float kNear = 0.1f;
    constexpr float kFar = 100000.0f;
    const Matrix projection{scale / aspect,
                            0,
                            0,
                            0,
                            0,
                            scale,
                            0,
                            0,
                            0,
                            0,
                            kFar / (kNear - kFar),
                            -1,
                            0,
                            0,
                            kNear * kFar / (kNear - kFar),
                            0};
    return multiply(view, projection);
}

Matrix inverse_matrix(const Matrix& matrix) {
    std::array<std::array<double, 8>, 4> rows{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            rows[row][column] = matrix[row * 4 + column];
        }
        rows[row][row + 4] = 1;
    }
    for (unsigned pivot = 0; pivot < 4; ++pivot) {
        eliminate(rows, pivot);
    }
    Matrix result{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            result[row * 4 + column] = float(rows[row][column + 4]);
        }
    }
    return result;
}

void Renderer::Impl::update_history(const FrameInput& input) {
    const auto index = 127u - static_cast<unsigned>(serial % 128u);
    const auto position = input.camera->pose().position;
    const auto& settings = *input.settings;
    const bool jitter = mode == settings::RenderVersion::albedo ? settings.render.albedo.taa_jitter
                        : mode == settings::RenderVersion::base
                            ? settings.render.base.taa_jitter
                            : settings.render.path_tracer.taa_jitter;
    const auto frame = static_cast<std::uint32_t>(serial % 32u) + 1u;
    history.jitter = jitter ? Vector2{halton(frame, 3) - 0.5f, halton(frame, 7) - 0.5f} : Vector2{};
    if (jitter && mode == settings::RenderVersion::path_tracer) {
        const auto seed = static_cast<std::uint32_t>(status.frame_count) * 32u;
        history.jitter = {random_unit(seed + 30u) - 0.5f, random_unit(seed + 31u) - 0.5f};
    }
    history.matrix =
        view_projection(*input.camera, float(surface_extent.width) / surface_extent.height);
    history.inverse = inverse_matrix(history.matrix);
    history.matrices[index] = history.matrix;
    history.inverses[index] = history.inverse;
    history.positions[index] = position;
    history.jitters[index] = history.jitter;
    for (unsigned slot = 0; slot < 128; ++slot) {
        const auto offset = history.positions[slot] - position;
        history.offsets[slot] = {float(offset.x), float(offset.y), float(offset.z)};
    }
}

} // namespace engine::rendering::vulkan
