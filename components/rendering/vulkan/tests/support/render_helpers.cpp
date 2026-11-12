#include "render_helpers.hpp"

#include "../../src/frame/values.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine::rendering::vulkan::test {
namespace {

using ProbeInput = std::array<float, 4>;
using ProbeResult = std::array<std::uint32_t, 4>;
constexpr auto kAbsent = std::numeric_limits<std::uint32_t>::max();

struct ProjectionCase {
    ProbeInput point;
    ProbeResult expected;
};

std::vector<ProjectionCase> projection_cases() {
    constexpr auto kInfinity = std::numeric_limits<float>::infinity();
    constexpr auto kNan = std::numeric_limits<float>::quiet_NaN();
    return {{{0, 0, 0.5F, 0}, {1, 1, 58, 3}},
            {{0, 1, 0.5F, 0}, {1, 1, 6, 1}},
            {{0, -1, 0.5F, 0}, {1, 0, kAbsent, kAbsent}},
            {{-1, 0, 0.5F, 0}, {1, 1, 52, 2}},
            {{1, 0, 0.5F, 0}, {1, 0, kAbsent, kAbsent}},
            {{-1, 1, 0.5F, 0}, {1, 1, 0, 0}},
            {{1, 1, 0.5F, 0}, {1, 0, kAbsent, kAbsent}},
            {{-1, -1, 0.5F, 0}, {1, 0, kAbsent, kAbsent}},
            {{0.5F, 0.5F, 0.5F, 0}, {1, 1, 35, 1}},
            {{-0.5F, -0.5F, 0.5F, 0}, {1, 1, 81, 2}},
            {{0, 0, 0, 0}, {1, 1, 58, 3}},
            {{0, 0, 1, 0}, {1, 1, 58, 3}},
            {{0, 0, -0.01F, 0}, {1, 0, kAbsent, kAbsent}},
            {{0, 0, 1.01F, 0}, {1, 0, kAbsent, kAbsent}},
            {{-1.01F, 0, 0.5F, 0}, {1, 0, kAbsent, kAbsent}},
            {{0, 1.01F, 0.5F, 0}, {1, 0, kAbsent, kAbsent}},
            {{kInfinity, 0, 0.5F, 0}, {0, 0, kAbsent, kAbsent}},
            {{0, kNan, 0.5F, 0}, {0, 0, kAbsent, kAbsent}},
            {{0, 0, kInfinity, 0}, {0, 0, kAbsent, kAbsent}}};
}

Matrix scaled_identity(float scale) {
    Matrix result{};
    for (const auto index : {0, 5, 10, 15}) {
        result[index] = scale;
    }
    return result;
}

std::vector<ProbeResult> dispatch(WorldCompute& compute, const std::string& entry,
                                  std::span<const ProbeInput> inputs, const UniformValues& values) {
    compute.upload("probeInputs", std::as_bytes(inputs));
    compute.allocate("probeOutput", inputs.size() * sizeof(ProbeResult), false);
    compute.run(entry, {static_cast<std::uint32_t>(inputs.size()), 1, 1}, values);
    const auto bytes = compute.read("probeOutput", 0, inputs.size() * sizeof(ProbeResult));
    std::vector<ProbeResult> result(inputs.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

void projection_contract(WorldCompute& compute, const Matrix& matrix, bool zero_w = false) {
    const auto cases = projection_cases();
    std::vector<ProbeInput> inputs;
    for (const auto& item : cases) {
        inputs.push_back(item.point);
    }
    UniformValues values;
    set_value(values, "camMatrix", matrix);
    set_value(values, "screenWidth", 13U);
    set_value(values, "screenHeight", 9U);
    set_value(values, "globalIlumBucketSizeX", 2U);
    set_value(values, "globalIlumBucketSizeY", 2U);
    const auto actual = dispatch(compute, "probeProjection", inputs, values);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto expected = zero_w ? ProbeResult{0, 0, kAbsent, kAbsent} : cases[index].expected;
        if (actual[index] != expected) {
            throw std::runtime_error("Production projection/bucket geometry differs in case " +
                                     std::to_string(index));
        }
    }
}

void upload_atmosphere(WorldCompute& compute) {
    // Half-precision fixture colors encode the LUT column/row and a constant blue.
    constexpr std::array<std::uint32_t, 5> kHalf{0, 0x3C00U, 0x4000U, 0x4200U, 0x4400U};
    std::array<std::array<std::uint32_t, 3>, 25> cells{};
    for (std::size_t index = 0; index < cells.size(); ++index) {
        cells[index] = {kHalf[index % 5] | (kHalf[index / 5] << 16), 0x3C003C00U, 0x3C003C00U};
    }
    compute.upload("atmosphereComp", std::as_bytes(std::span(cells)));
}

void atmosphere_contract(WorldCompute& compute) {
    const std::array<ProbeInput, 8> rays{{{0, 1, 0, 0},
                                          {0, -1, 0, 0},
                                          {1, 0, 0, 0},
                                          {-1, 0, 0, 0},
                                          {0, 0, 1, 0},
                                          {0, 0, -1, 0},
                                          {0.6F, 0.8F, 0, 0},
                                          {0.6F, -0.8F, 0, 0}}};
    const std::array<std::array<float, 3>, 8> expected{
        {{2, 4, 1}, {2, 0, 1}, {4, 2, 1}, {0, 2, 1}, {2, 2, 1}, {4, 4, 1}, {2, 3, 1}, {2, 0, 1}}};
    upload_atmosphere(compute);
    UniformValues values;
    set_value(values, "atmosphereTexSizeX", 5);
    set_value(values, "atmosphereTexSizeY", 5);
    const auto actual = dispatch(compute, "probeAtmosphere", rays, values);
    for (std::size_t index = 0; index < rays.size(); ++index) {
        if (actual[index][3] != 1) {
            throw std::runtime_error("Production atmosphere returned nonfinite light/absorption");
        }
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto value = std::bit_cast<float>(actual[index][channel]);
            if (!std::isfinite(value) || std::abs(value - expected[index][channel]) > 1e-5F) {
                throw std::runtime_error("Production atmosphere chose the wrong pole/cardinal "
                                         "LUT cell in case " +
                                         std::to_string(index));
            }
        }
    }
}

void math_contract(WorldCompute& compute) {
    const std::array<ProbeInput, 3> inputs{
        {{1.5F, 1, 2, 10}, {1.5F, 0, 3, 10}, {1.5F, 0.5F, 0, 10}}};
    constexpr std::array<float, 3> kReflectance{0.04F, 1, 0.07F};
    constexpr float kGaussianPeak = 0.00159154943F;
    std::array<float, 3> gaussian{};
    const auto actual = dispatch(compute, "probeMath", inputs, {});
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto fresnel = std::bit_cast<float>(actual[index][0]);
        const auto positive = std::bit_cast<float>(actual[index][1]);
        const auto negative = std::bit_cast<float>(actual[index][2]);
        if (actual[index][3] != 1 || !std::isfinite(fresnel) || !std::isfinite(positive) ||
            !std::isfinite(negative) || std::abs(fresnel - kReflectance[index]) > 1e-5F ||
            positive <= 0 || negative <= 0 || std::abs(positive - negative) > 1e-7F) {
            throw std::runtime_error("Production Fresnel/Gaussian differs in case " +
                                     std::to_string(index));
        }
        gaussian[index] = positive;
    }
    if (!(gaussian[1] < gaussian[0] && gaussian[0] < gaussian[2]) ||
        std::abs(gaussian[2] - kGaussianPeak) > 1e-7F) {
        throw std::runtime_error("Production Gaussian is not symmetric around the expected peak");
    }
}

std::int32_t mirror_reference(std::int32_t position, std::uint32_t extent) {
    const std::int64_t period = std::max(1U, extent) * std::int64_t{2};
    const auto wrapped = ((position % period) + period) % period;
    const auto mirrored = wrapped > extent ? period - wrapped : wrapped;
    return static_cast<std::int32_t>(std::min(mirrored, std::int64_t{extent} - 1));
}

std::vector<ProbeInput> mirror_inputs() {
    std::vector<ProbeInput> inputs;
    const auto append = [&inputs](std::int32_t position, std::uint32_t extent) {
        inputs.push_back({std::bit_cast<float>(position), std::bit_cast<float>(extent), 0, 0});
    };
    for (const auto extent : {0U, 1U, 2U, 3U, 8U, 13U, 64U, 127U, 320U, 900U, 1600U, 4096U}) {
        const auto boundary = static_cast<std::int32_t>(extent * 4U + 3U);
        for (auto position = -boundary; position <= boundary; ++position) {
            append(position, extent);
        }
        append(std::numeric_limits<std::int32_t>::min(), extent);
        append(std::numeric_limits<std::int32_t>::max(), extent);
    }
    for (const auto extent : {16384U, 32768U, 65536U}) {
        for (const auto multiple : {-9, -4, -2, -1, 0, 1, 2, 4, 9}) {
            for (const auto offset : {-1, 0, 1}) {
                append(multiple * static_cast<std::int32_t>(extent) + offset, extent);
            }
        }
        append(std::numeric_limits<std::int32_t>::min(), extent);
        append(std::numeric_limits<std::int32_t>::max(), extent);
    }
    return inputs;
}

void mirror_contract(WorldCompute& compute) {
    const auto inputs = mirror_inputs();
    UniformValues values;
    set_value(values, "probeMathMode", 1U);
    const auto actual = dispatch(compute, "probeMath", inputs, values);
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto position = std::bit_cast<std::int32_t>(inputs[index][0]);
        const auto extent = std::bit_cast<std::uint32_t>(inputs[index][1]);
        const auto expected = std::bit_cast<std::uint32_t>(mirror_reference(position, extent));
        if (actual[index] != ProbeResult{expected, 0, 0, 0}) {
            throw std::runtime_error("Production mirrored screen coordinate differs in case " +
                                     std::to_string(index));
        }
    }
    std::cout << "Production mirrored pixels match the signed periodic reference in "
              << inputs.size() << " edge/interior/multiple-period cases\n";
}

} // namespace

void verify_render_helpers(WorldCompute& compute) {
    projection_contract(compute, scaled_identity(1));
    projection_contract(compute, scaled_identity(1e-25F));
    auto zero_w = scaled_identity(1);
    zero_w[15] = 0;
    projection_contract(compute, zero_w, true);
    atmosphere_contract(compute);
    math_contract(compute);
    mirror_contract(compute);
    std::cout << "Production projection: center, borders, depth, finite/tiny/zero w; atmosphere "
                 "cardinals/poles selected the expected LUT cells; Fresnel and symmetric "
                 "Gaussian values are finite\n";
}

} // namespace engine::rendering::vulkan::test
