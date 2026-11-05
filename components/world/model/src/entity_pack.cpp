#include "helpers.hpp"

#include <algorithm>
#include <cmath>

namespace engine::world::model {

Vec3 rotate(Vec3 point, const std::array<float, 4>& quaternion) {
    const Vec3 axis{quaternion[0], quaternion[1], quaternion[2]};
    const auto twice_cross = foundation::geometry::cross(axis, point) * 2;
    return point + twice_cross * quaternion[3] + foundation::geometry::cross(axis, twice_cross);
}

std::array<std::uint32_t, 2> pack_quaternion(std::array<float, 4> quaternion) {
    unsigned largest = 0;
    for (unsigned index = 1; index < 4; ++index) {
        if (std::abs(quaternion[index]) > std::abs(quaternion[largest])) {
            largest = index;
        }
    }
    const auto sign = quaternion[largest] < 0 ? -1.F : 1.F;
    std::array<std::uint32_t, 3> small{};
    unsigned destination = 0;
    for (unsigned index = 0; index < 4; ++index) {
        if (index != largest) {
            const auto quantized = (quaternion[index] * sign + 1.F) * 8192.F + 0.5F;
            small[destination++] = static_cast<std::uint32_t>(std::clamp(quantized, 0.F, 16383.F));
        }
    }
    return {small[0] | (small[1] << 14) | ((small[2] & 15U) << 28),
            (small[2] >> 4) | (largest << 10)};
}

} // namespace engine::world::model
