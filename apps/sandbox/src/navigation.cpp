#include "application.hpp"
#include <cmath>
namespace engine::app::sandbox {
namespace {
void commands(Application& app, const interaction::input::Actions& actions) {
    using interaction::input::Command;
    if (actions.wants(Command::toggle_path)) {
        if (app.path.playing()) {
            app.path.stop();
        } else {
            app.path.start();
        }
    }
    if (actions.wants(Command::toggle_sampling)) {
        using rendering::settings::RenderVersion;
        auto& version = app.settings.render.version;
        version = version == RenderVersion::path_tracer ? RenderVersion::base
                                                        : RenderVersion::path_tracer;
    }
}
void play_path(Application& app, double seconds) {
    const auto sample = app.path.advance(seconds, app.ui->linear_path());
    motion::camera::Pose pose;
    pose.position = sample.position;
    pose.yaw = std::atan2(sample.direction.x, sample.direction.z);
    pose.pitch = std::atan2(sample.direction.y, std::hypot(sample.direction.x, sample.direction.z));
    if (const auto result = app.camera->set_pose(pose); !result) {
        app.metrics.status = result.error();
    }
    if (sample.final_frame && app.ui->screenshot_at_end()) {
        app.screenshot_requested = true;
    }
}
} // namespace
void update_navigation(Application& app, const interaction::input::Actions& actions,
                       double seconds) {
    commands(app, actions);
    app.paused = actions.pause_sampling;
    if (actions.sun_delta != 0) {
        app.settings.atmosphere.sun_angle +=
            static_cast<float>(actions.sun_delta * seconds * 9 * (actions.boost ? 5 : 1));
    }
    if (app.path.playing()) {
        play_path(app, seconds);
        return;
    }
    app.camera->rotate({actions.look_x, actions.look_y});
    app.camera->fly({actions.forward, actions.right, actions.vertical, actions.boost}, seconds);
}
} // namespace engine::app::sandbox
