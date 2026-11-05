#ifndef ENGINE_FOUNDATION_GEOMETRY_VECTOR_HPP
#define ENGINE_FOUNDATION_GEOMETRY_VECTOR_HPP

#include <array>
#include <cmath>
#include <cstddef>

namespace engine::foundation::geometry {

struct Vec3 {
    double x = 0;
    double y = 0;
    double z = 0;

    [[nodiscard]] constexpr double operator[](std::size_t axis) const {
        return axis == 0 ? x : (axis == 1 ? y : z);
    }
    constexpr double& operator[](std::size_t axis) {
        return axis == 0 ? x : (axis == 1 ? y : z);
    }
    constexpr bool operator==(const Vec3&) const = default;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 left, Vec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}
[[nodiscard]] constexpr Vec3 operator-(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}
[[nodiscard]] constexpr Vec3 operator*(Vec3 value, double scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}
[[nodiscard]] constexpr Vec3 operator/(Vec3 value, double scale) {
    return value * (1.0 / scale);
}
[[nodiscard]] constexpr double dot(Vec3 left, Vec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 left, Vec3 right) {
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}
[[nodiscard]] inline double length(Vec3 value) {
    return std::sqrt(dot(value, value));
}
[[nodiscard]] inline bool finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
[[nodiscard]] inline Vec3 normalized(Vec3 value) {
    const auto norm = length(value);
    return norm > 0 ? value / norm : Vec3{};
}
[[nodiscard]] constexpr Vec3 interpolate(Vec3 before, Vec3 after, double alpha) {
    return before * (1 - alpha) + after * alpha;
}

struct Box {
    Vec3 lower;
    Vec3 upper;
};

[[nodiscard]] constexpr Box translated(Box box, Vec3 distance) {
    return {box.lower + distance, box.upper + distance};
}

} // namespace engine::foundation::geometry
#endif
