#include <engine/foundation/geometry/vector.hpp>

#include <cmath>
#include <iostream>

int main() {
    using engine::foundation::geometry::cross;
    using engine::foundation::geometry::dot;
    using engine::foundation::geometry::length;
    using engine::foundation::geometry::normalized;
    using engine::foundation::geometry::Vec3;
    const auto direction = normalized(Vec3{3, 4, 0});
    const bool passed = std::abs(length(direction) - 1) < 1e-15 &&
                        dot(cross(Vec3{0, 0, 1}, Vec3{0, 1, 0}), Vec3{-1, 0, 0}) == 1;
    if (!passed) {
        std::cerr << "Geometry contracts failed\n";
        return 1;
    }
    return 0;
}
