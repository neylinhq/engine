#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace engine::app::benchmark {
std::vector<Keyframe> parse_camera(const json::array& values, unsigned frames) {
    if (values.empty()) {
        throw std::invalid_argument("At least one camera keyframe is required");
    }
    std::vector<Keyframe> result;
    for (const auto& value : values) {
        const auto& object = value.as_object();
        keys(object, {"frame", "position", "yaw", "pitch"});
        Keyframe key;
        key.frame = integer(object.at("frame"), 0, frames - 1);
        key.pose.position = vector(object.at("position"));
        key.pose.yaw = real(object.at("yaw"), -1e6, 1e6);
        key.pose.pitch =
            real(object.at("pitch"), -motion::camera::kMaximumPitch, motion::camera::kMaximumPitch);
        if ((result.empty() && key.frame != 0) ||
            (!result.empty() && key.frame <= result.back().frame)) {
            throw std::invalid_argument("Camera keys must start at frame 0 and increase strictly");
        }
        result.push_back(key);
    }
    return result;
}
motion::camera::Pose camera_at(const std::vector<Keyframe>& camera, unsigned frame) {
    const auto end = std::ranges::upper_bound(camera, frame, {}, &Keyframe::frame);
    if (end == camera.end()) {
        return camera.back().pose;
    }
    const auto& left = *(end - 1);
    const auto& right = *end;
    const auto fraction = static_cast<double>(frame - left.frame) / (right.frame - left.frame);
    return {{std::lerp(left.pose.position.x, right.pose.position.x, fraction),
             std::lerp(left.pose.position.y, right.pose.position.y, fraction),
             std::lerp(left.pose.position.z, right.pose.position.z, fraction)},
            std::lerp(left.pose.yaw, right.pose.yaw, fraction),
            std::lerp(left.pose.pitch, right.pose.pitch, fraction)};
}
} // namespace engine::app::benchmark
