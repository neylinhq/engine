#ifndef ENGINE_MOTION_CAMERA_CAMERA_HPP
#define ENGINE_MOTION_CAMERA_CAMERA_HPP

#include <engine/foundation/geometry/vector.hpp>
#include <expected>
#include <string>

namespace engine::motion::camera {
using foundation::geometry::Vec3;

inline constexpr double kMaximumPitch = 1.55;

struct Pose {
    Vec3 position;
    double yaw = 0;
    double pitch = 0;
};
struct Basis {
    Vec3 forward{0, 0, 1};
    Vec3 right{-1, 0, 0};
    Vec3 up{0, 1, 0};
};
struct PointerDelta {
    double x = 0;
    double y = 0;
};
struct FlightIntent {
    double forward = 0;
    double right = 0;
    double vertical = 0;
    bool boost = false;
};
struct Pixel {
    double x = 0;
    double y = 0;
};
struct Viewport {
    unsigned width = 1600;
    unsigned height = 900;
};
struct Settings {
    double speed = 2;
    double maximum_boost = 24;
    double fov_degrees = 60;
};

class Camera {
  public:
    [[nodiscard]] static std::expected<Camera, std::string> create(double world_side, Pose pose);
    [[nodiscard]] std::expected<void, std::string> set_pose(Pose pose);
    [[nodiscard]] std::expected<void, std::string> configure(Settings settings);
    // World size determines the speed range; free flight is not bounded by the voxel volume.
    [[nodiscard]] std::expected<void, std::string> reframe(double world_side);
    void rotate(PointerDelta motion);
    void fly(FlightIntent intent, double wall_seconds);
    void translate(Vec3 axis, double meters);
    void clear_changed();
    [[nodiscard]] Pose pose() const;
    [[nodiscard]] Basis basis() const;
    [[nodiscard]] Settings settings() const;
    [[nodiscard]] double boost() const;
    [[nodiscard]] bool changed() const;
    [[nodiscard]] std::expected<Vec3, std::string> direction(Pixel pixel, Viewport viewport) const;
    [[nodiscard]] std::expected<Pixel, std::string> project(Vec3 point, Viewport viewport) const;

  private:
    Camera(double world_side, Pose pose);
    void update_basis();
    double world_side_;
    Pose pose_;
    Basis basis_;
    Settings settings_;
    double boost_ = 1;
    bool changed_ = false;
};

} // namespace engine::motion::camera
#endif
