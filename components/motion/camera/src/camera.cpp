#include <engine/motion/camera/camera.hpp>

#include <algorithm>
#include <cmath>

namespace engine::motion::camera {
Camera::Camera(double world_side, Pose pose) : world_side_(world_side), pose_(pose) {
    settings_.speed = std::max(2.0, world_side / 400.0);
}
std::expected<Camera, std::string> Camera::create(double world_side, Pose pose) {
    if (!std::isfinite(world_side) || world_side <= 0) {
        return std::unexpected("Invalid camera world size");
    }
    Camera camera(world_side, pose);
    if (const auto result = camera.set_pose(pose); !result) {
        return std::unexpected(result.error());
    }
    camera.clear_changed();
    return camera;
}
std::expected<void, std::string> Camera::set_pose(Pose pose) {
    if (!foundation::geometry::finite(pose.position)) {
        return std::unexpected("Camera position must be finite");
    }
    pose.yaw = std::isfinite(pose.yaw) ? pose.yaw : 0;
    pose.pitch =
        std::isfinite(pose.pitch) ? std::clamp(pose.pitch, -kMaximumPitch, kMaximumPitch) : 0;
    changed_ = changed_ || pose_.position != pose.position || pose_.yaw != pose.yaw ||
               pose_.pitch != pose.pitch;
    pose_ = pose;
    boost_ = 1;
    update_basis();
    return {};
}
std::expected<void, std::string> Camera::configure(Settings settings) {
    if (!std::isfinite(settings.speed) || !std::isfinite(settings.maximum_boost) ||
        (settings.speed != settings_.speed &&
         (settings.speed < 0.1 || settings.speed > world_side_ / 20)) ||
        settings.maximum_boost < 1 || settings.maximum_boost > 64 ||
        !std::isfinite(settings.fov_degrees) || settings.fov_degrees < 1 ||
        settings.fov_degrees > 120) {
        return std::unexpected("Camera settings outside their declared range");
    }
    changed_ = changed_ || settings.fov_degrees != settings_.fov_degrees;
    settings_ = settings;
    return {};
}
std::expected<void, std::string> Camera::reframe(double world_side) {
    if (!std::isfinite(world_side) || world_side <= 0) {
        return std::unexpected("Invalid camera world size");
    }
    world_side_ = world_side;
    return {};
}
void Camera::update_basis() {
    const auto cosine = std::cos(pose_.pitch);
    basis_.forward = foundation::geometry::normalized(
        Vec3{cosine * std::sin(pose_.yaw), std::sin(pose_.pitch), cosine * std::cos(pose_.yaw)});
    basis_.right = foundation::geometry::normalized(
        foundation::geometry::cross(basis_.forward, Vec3{0, 1, 0}));
    basis_.up =
        foundation::geometry::normalized(foundation::geometry::cross(basis_.right, basis_.forward));
}
void Camera::rotate(PointerDelta motion) {
    const auto yaw = pose_.yaw;
    const auto pitch = pose_.pitch;
    pose_.yaw -= 0.0022 * motion.x;
    pose_.pitch = std::clamp(pose_.pitch - 0.0022 * motion.y, -kMaximumPitch, kMaximumPitch);
    changed_ = changed_ || std::abs(pose_.yaw - yaw) > 1e-7 || std::abs(pose_.pitch - pitch) > 1e-7;
    update_basis();
}
void Camera::fly(FlightIntent intent, double wall_seconds) {
    const auto delta = std::clamp(wall_seconds, 0.0, 0.1);
    const auto target = intent.boost ? settings_.maximum_boost : 1;
    const auto rate = intent.boost ? 1.6 : 4.0;
    boost_ += (target - boost_) * std::min(1.0, rate * delta);
    const auto movement =
        basis_.forward * intent.forward + basis_.right * intent.right + Vec3{0, intent.vertical, 0};
    const auto before = pose_.position;
    if (foundation::geometry::dot(movement, movement) > 1e-6) {
        pose_.position = before + foundation::geometry::normalized(movement) *
                                      (settings_.speed * boost_ * delta);
    }
    changed_ = changed_ || foundation::geometry::length(pose_.position - before) > 1e-6;
}
void Camera::translate(Vec3 axis, double metres) {
    pose_.position = pose_.position + axis * metres;
    changed_ = true;
}
void Camera::clear_changed() {
    changed_ = false;
}
Pose Camera::pose() const {
    return pose_;
}
Basis Camera::basis() const {
    return basis_;
}
Settings Camera::settings() const {
    return settings_;
}
double Camera::boost() const {
    return boost_;
}
bool Camera::changed() const {
    return changed_;
}
} // namespace engine::motion::camera
