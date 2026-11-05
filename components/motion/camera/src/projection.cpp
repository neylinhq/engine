#include <engine/motion/camera/camera.hpp>

#include <cmath>
#include <numbers>

namespace engine::motion::camera {
std::expected<Vec3, std::string> Camera::direction(Pixel pixel, Viewport viewport) const {
    if (viewport.width == 0 || viewport.height == 0 || !std::isfinite(pixel.x) ||
        !std::isfinite(pixel.y)) {
        return std::unexpected("Invalid projection extent or pixel");
    }
    const auto tangent = std::tan(settings_.fov_degrees * std::numbers::pi / 360);
    const auto horizontal = (2 * pixel.x / viewport.width - 1) * viewport.width / viewport.height;
    const auto vertical = 1 - 2 * pixel.y / viewport.height;
    return foundation::geometry::normalized(basis_.forward + basis_.right * (horizontal * tangent) +
                                            basis_.up * (vertical * tangent));
}
std::expected<Pixel, std::string> Camera::project(Vec3 point, Viewport viewport) const {
    if (viewport.width == 0 || viewport.height == 0 || !foundation::geometry::finite(point)) {
        return std::unexpected("Invalid projection input");
    }
    const auto difference = point - pose_.position;
    const auto depth = foundation::geometry::dot(difference, basis_.forward);
    if (depth <= 1e-4) {
        return std::unexpected("Point is not in front of the projection plane");
    }
    const auto scale =
        viewport.height / (2 * depth * std::tan(settings_.fov_degrees * std::numbers::pi / 360));
    return Pixel{viewport.width * 0.5 + foundation::geometry::dot(difference, basis_.right) * scale,
                 viewport.height * 0.5 - foundation::geometry::dot(difference, basis_.up) * scale};
}
} // namespace engine::motion::camera
