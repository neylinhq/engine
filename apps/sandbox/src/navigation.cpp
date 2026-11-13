#include "application.hpp"
#include <cmath>
#include <engine/foundation/geometry/vector.hpp>
#include <engine/world/collision/terrain.hpp>
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
void walk(Application& app, const interaction::input::Actions& actions, double seconds) {
    const auto pose = app.camera->pose();
    const foundation::geometry::Vec3 forward{std::sin(pose.yaw), 0, std::cos(pose.yaw)};
    const auto right = app.camera->basis().right;
    const motion::character::Intent intent{.horizontal =
                                               forward * actions.forward + right * actions.right,
                                           .run = actions.boost,
                                           .jump_pressed = actions.jump_pressed};
    const world::collision::Terrain terrain{app.world.view(), app.options.voxel_meters};
    if (const auto result = app.character->advance(terrain, intent, seconds); !result) {
        app.metrics.status = result.error();
        return;
    }
    auto next = pose;
    next.position = app.character->state().eye / app.options.voxel_meters;
    if (const auto result = app.camera->set_pose(next); !result) {
        app.metrics.status = result.error();
    }
}
} // namespace
void start_walking(Application& app) {
    auto pose = app.camera->pose();
    const auto eye = pose.position * app.options.voxel_meters;
    motion::character::Motor character(
        eye - foundation::geometry::Vec3{0, motion::character::kEyeHeight, 0});
    const world::collision::Terrain terrain{app.world.view(), app.options.voxel_meters};
    if (const auto placed = character.advance(terrain, {}, 0); !placed) {
        app.character.reset();
        app.metrics.status = "Cannot enter walk mode: " + placed.error();
        return;
    }
    pose.position = character.state().eye / app.options.voxel_meters;
    if (const auto positioned = app.camera->set_pose(pose); !positioned) {
        app.character.reset();
        app.metrics.status = positioned.error();
        return;
    }
    app.character = character;
}
void update_navigation(Application& app, const interaction::input::Actions& actions,
                       double seconds) {
    commands(app, actions);
    app.paused = actions.pause_sampling;
    if (actions.sun_delta != 0) {
        app.settings.atmosphere.sun_angle +=
            static_cast<float>(actions.sun_delta * seconds * 9 * (actions.boost ? 5 : 1));
    }
    if (app.path.playing()) {
        app.character.reset();
        play_path(app, seconds);
        return;
    }
    if (actions.wants(interaction::input::Command::toggle_navigation)) {
        if (app.character) {
            app.character.reset();
            app.metrics.status = "Free flight";
        } else {
            start_walking(app);
            if (app.character) {
                app.metrics.status = "Walk";
            }
        }
    }
    app.camera->rotate({actions.look_x, actions.look_y});
    if (app.character) {
        walk(app, actions, seconds);
    } else {
        app.camera->fly({actions.forward, actions.right, actions.vertical, actions.boost}, seconds);
    }
}
} // namespace engine::app::sandbox
