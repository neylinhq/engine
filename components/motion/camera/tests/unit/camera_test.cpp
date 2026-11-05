#include <cmath>
#include <engine/motion/camera/camera.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void exercise() {
    using engine::foundation::geometry::Vec3;
    auto camera = engine::motion::camera::Camera::create(256, {{128, 128, 128}, 0, 0});
    require(camera.has_value(), "Camera creation failed");
    require(camera->basis().forward == Vec3{0, 0, 1} && camera->basis().right == Vec3{-1, 0, 0},
            "Camera basis signs disagree with the contract");
    const auto center = camera->direction({800, 450}, {1600, 900});
    require(center.has_value() && *center == Vec3{0, 0, 1}, "Aim is not geometric center");
    const auto position = camera->pose().position;
    camera->fly({1, 1, 0, false}, 0.1);
    require(std::abs(engine::foundation::geometry::length(camera->pose().position - position) -
                     0.2) < 1e-12,
            "Diagonal flight increased speed");
    camera->fly({0, 0, 0, true}, 0.1);
    require(std::abs(camera->boost() - 4.68) < 1e-12, "Boost ramp changed discrete rule");
    camera->fly({}, 0.1);
    require(std::abs(camera->boost() - 3.208) < 1e-12, "Boost release ramp incorrect");
    camera->rotate({10, -10});
    require(std::abs(camera->pose().yaw + 0.022) < 1e-12 &&
                std::abs(camera->pose().pitch - 0.022) < 1e-12,
            "Mouse sign or sensitivity wrong");
    require(!camera->direction({}, {0, 0}).has_value(), "Zero image size accepted");
}
void unrestricted_flight() {
    using engine::foundation::geometry::Vec3;
    auto camera = engine::motion::camera::Camera::create(256, {{-1, 900, 5000}, 0, 0}).value();
    require(camera.pose().position == Vec3{-1, 900, 5000}, "Initial pose was clamped to the world");
    require(camera.set_pose({{256, 0, 256}, 0, 0}).has_value(), "Boundary pose rejected");
    camera.fly({1, -1, -1, false}, 0.1);
    require(camera.pose().position.x > 256 && camera.pose().position.y < 0 &&
                camera.pose().position.z > 256,
            "Flight must cross the world boundary on every axis");
    camera.translate({-1, 1, -1}, 1000);
    require(camera.pose().position.x < 0 && camera.pose().position.y > 256 &&
                camera.pose().position.z < 0,
            "World translation must not clamp the camera");
    camera.fly({0, 0, 0, true}, 0.1);
    const auto position = camera.pose().position;
    const auto speed = camera.settings().speed;
    const auto boost = camera.boost();
    require(camera.reframe(64).has_value(), "World resize failed");
    require(camera.pose().position == position && camera.settings().speed == speed &&
                camera.boost() == boost,
            "World resize must preserve the free-flight state");
}
void configurable_projection() {
    using engine::foundation::geometry::dot;
    auto camera = engine::motion::camera::Camera::create(256, {{128, 128, 128}, 0, 0}).value();
    auto settings = camera.settings();
    settings.fov_degrees = 90;
    require(camera.configure(settings).has_value(), "Original 90 degree FOV rejected");
    const auto ray = camera.direction({100, 50}, {100, 100}).value();
    require(std::abs(dot(ray, camera.basis().right) / dot(ray, camera.basis().forward) - 1) < 1e-12,
            "Configurable FOV does not match projection");
    const auto pixel = camera.project(camera.pose().position + ray * 10, {100, 100}).value();
    require(std::abs(pixel.x - 100) < 1e-12 && std::abs(pixel.y - 50) < 1e-12,
            "Projection and ray reconstruction disagree");
    settings.fov_degrees = 0;
    require(!camera.configure(settings), "Invalid FOV accepted");
}
void pose_changes() {
    auto camera = engine::motion::camera::Camera::create(256, {{128, 128, 128}, 0, 0}).value();
    require(camera.set_pose(camera.pose()).has_value(), "Unchanged pose rejected");
    require(!camera.changed(), "Unchanged pose must preserve render accumulation");
    auto pose = camera.pose();
    pose.position.x += 1;
    require(camera.set_pose(pose).has_value() && camera.changed(),
            "Camera translation did not invalidate accumulation");
    require(camera.set_pose(pose).has_value() && camera.changed(),
            "Repeated pose cleared an unconsumed view change");
    camera.rotate({});
    require(camera.changed(), "Idle pointer input cleared an unconsumed view change");
    camera.clear_changed();
    pose.yaw = 0.25;
    require(camera.set_pose(pose).has_value() && camera.changed(),
            "Camera rotation did not invalidate accumulation");
    camera.clear_changed();
    auto settings = camera.settings();
    settings.fov_degrees = 90;
    require(camera.configure(settings).has_value(), "Valid FOV rejected");
    require(camera.set_pose(pose).has_value() && camera.changed(),
            "Repeated pose cleared a projection change");
}
void normalized_pose_changes() {
    auto camera = engine::motion::camera::Camera::create(256, {{}, 0, 0}).value();
    for (const auto pitch : {-2.0, 2.0}) {
        auto pose = camera.pose();
        pose.pitch = pitch;
        require(camera.set_pose(pose).has_value() && camera.changed(),
                "Effective pitch change did not invalidate accumulation");
        require(camera.pose().pitch == (pitch < 0 ? -1.55 : 1.55), "Camera pitch limit changed");
        camera.clear_changed();
        require(camera.set_pose(pose).has_value() && !camera.changed(),
                "Equivalent clamped pose invalidated accumulation");
    }
}
} // namespace
int main() {
    try {
        exercise();
        configurable_projection();
        unrestricted_flight();
        pose_changes();
        normalized_pose_changes();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
