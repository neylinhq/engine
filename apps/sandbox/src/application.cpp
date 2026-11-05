#include "application.hpp"
#include "io/path.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace engine::app::sandbox {
namespace {
bool original_seed(world::model::IVec3 size) {
    return size.x > 500 && size.y > 200 && size.z > 40;
}
motion::camera::Pose starting_pose(world::model::IVec3 size) {
    if (original_seed(size)) {
        return {{500, 200, 40}, 0, 0};
    }
    const auto scale = static_cast<double>(std::max({size.x, size.y, size.z, 16}));
    return {{size.x * 0.5, size.y + scale * 0.45, size.z * 0.5 - scale * 1.2}, 0, 0};
}
void aim_at_model(motion::camera::Camera& camera, world::model::IVec3 size) {
    auto pose = camera.pose();
    const foundation::geometry::Vec3 center{size.x * 0.5, size.y * 0.5, size.z * 0.5};
    const auto direction = center - pose.position;
    pose.yaw = std::atan2(direction.x, direction.z);
    pose.pitch = std::atan2(direction.y, std::hypot(direction.x, direction.z));
    static_cast<void>(camera.set_pose(pose));
}
void require(std::expected<void, std::string> result) {
    if (!result) {
        throw std::runtime_error(result.error());
    }
}
void synchronize_capture(Application& app) {
    const auto state = app.router.state();
    if (state.captured != app.captured) {
        app.captured = state.captured;
        app.window->capture(app.captured);
        app.router.rebase_pointer();
    }
    app.ui->set_visible(state.debug_open && !app.options.no_ui);
    app.ui->accept_input(app.ui->visible() && !state.captured);
}
void finish_capture(Application& app) {
    app.screenshot_requested = false;
    try {
        auto path = app.options.screenshot;
        if (path.empty()) {
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            path = "out/user/screenshot_" + std::to_string(stamp) + ".png";
        }
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        require(app.renderer->screenshot(path));
        app.metrics.status = "Screenshot saved: " + path_to_utf8(path);
        std::cout << app.metrics.status << '\n';
    } catch (const std::exception& error) {
        app.metrics.status = "Screenshot failed: " + std::string(error.what());
        if (app.options.frames != 0) {
            throw;
        }
        std::cerr << app.metrics.status << '\n';
    }
}
void render(Application& app, double seconds) {
    const auto final = app.options.frames != 0 && app.frame + 1 == app.options.frames;
    rendering::vulkan::FrameInput input;
    input.world = app.world.view();
    input.entities = &app.world.entity_gpu();
    input.camera = &*app.camera;
    input.settings = &app.settings;
    input.delta_seconds = seconds;
    input.active = app.active;
    input.paused = app.paused;
    input.capture_scene = app.screenshot_requested;
    input.capture_output = final && !app.options.screenshot.empty();
    input.reset_accumulation = app.reset;
    input.bounds_budget = static_cast<unsigned>(app.ui->bounds_speedup()) * 512U;
    require(app.renderer->draw(input));
    if (app.active) {
        app.world.acknowledge_changes();
        app.reset = false;
    }
    ++app.frame;
    if (app.screenshot_requested || (final && !app.options.screenshot.empty())) {
        finish_capture(app);
    }
}
void free_pointer(Application& app, interaction::input::Snapshot& snapshot) {
    const auto& editing = app.ui->editing();
    const auto available = app.pointer_available && !app.router.state().captured &&
                           editing.tool != world::editing::Tool::none && !snapshot.pointer.right &&
                           !snapshot.down(interaction::input::Key::escape);
    if (!available || !snapshot.pointer.left) {
        app.free_edit_gesture = false;
    } else if (app.left_pressed) {
        app.free_edit_gesture = true;
    }
    const auto repeat = editing.continuous && editing.tool != world::editing::Tool::model &&
                        editing.tool != world::editing::Tool::flood_fill;
    app.free_edit = app.free_edit_gesture && (repeat || app.left_pressed);
    if (available) {
        snapshot.access.pointer_owned = true;
    }
}
void pointer_snapshot(Application& app, interaction::input::Snapshot& snapshot) {
    app.pointer_available = snapshot.focused && snapshot.access.interactive &&
                            !snapshot.access.pointer_owned && !snapshot.access.text_editing;
    app.left_pressed = snapshot.pointer.left && !app.left_down;
    app.left_released = !snapshot.pointer.left && app.left_down;
    app.left_down = snapshot.focused && snapshot.pointer.left;
    const auto& editing = app.ui->editing();
    if (editing.pick_material || editing.spawning) {
        app.router.set_capture(false);
        snapshot.access.pointer_owned = true;
        app.free_edit = false;
        app.free_edit_gesture = false;
    } else {
        free_pointer(app, snapshot);
    }
}
void update_pointer_ray(Application& app, const interaction::input::Snapshot& snapshot) {
    app.pointer_ray = app.camera->basis().forward;
    if (app.router.state().captured) {
        return;
    }
    const auto extent = app.window->logical_extent();
    const auto ray = app.camera->direction({snapshot.pointer.x + 0.5, snapshot.pointer.y + 0.5},
                                           {extent.width, extent.height});
    if (ray) {
        app.pointer_ray = *ray;
    }
}
void cancel_interaction(Application& app, const interaction::input::Snapshot& snapshot) {
    if (!snapshot.down(interaction::input::Key::escape)) {
        return;
    }
    app.ui->editing().pick_material = false;
    if (app.ui->editing().spawning) {
        app.world.remove_entities_from(app.ui->editing().spawn_begin);
        app.ui->editing().spawning = false;
    }
}
void update_motion(Application& app, interaction::input::Snapshot& snapshot, double seconds) {
    snapshot.access = app.ui->access();
    pointer_snapshot(app, snapshot);
    const auto& editing = app.ui->editing();
    const auto repeat = editing.continuous && editing.tool != world::editing::Tool::model &&
                        editing.tool != world::editing::Tool::flood_fill;
    auto actions = app.router.poll(snapshot, {repeat, false});
    if (!actions) {
        app.metrics.status = actions.error();
        return;
    }
    synchronize_capture(app);
    if (!app.active) {
        return;
    }
    cancel_interaction(app, snapshot);
    update_navigation(app, *actions, seconds);
    update_pointer_ray(app, snapshot);
    const auto was_spawning = app.ui->editing().spawning;
    update_entities(app);
    if (!was_spawning) {
        update_editing(app, *actions, seconds);
    }
}
void update_metrics(Application& app) {
    const auto stats = app.renderer->statistics();
    app.metrics.render_width = stats.width;
    app.metrics.render_height = stats.height;
    app.metrics.gpu_milliseconds = stats.gpu_milliseconds;
    app.metrics.gpu_bytes = stats.allocated_bytes;
    app.metrics.chunk_storage =
        stats.chunk_layout == rendering::vulkan::ChunkLayout::packed ? "Packed RG32" : "Split R32";
    app.metrics.output_path = stats.direct_output ? "Direct swapchain" : "Offscreen copy";
}
bool path_settings_changed(const rendering::settings::PathTracer& before,
                           const rendering::settings::PathTracer& after) {
    return before.bounce_count != after.bounce_count || before.taa_jitter != after.taa_jitter ||
           before.atmosphere_interaction != after.atmosphere_interaction;
}
void update_camera_settings(Application& app, const rendering::settings::PathTracer& previous) {
    if (!app.active) {
        return;
    }
    auto configuration = app.camera->settings();
    configuration.fov_degrees = app.settings.general.fov;
    require(app.camera->configure(configuration));
    app.reset = app.reset || app.camera->changed() ||
                path_settings_changed(previous, app.settings.render.path_tracer);
}
void advance_entities(Application& app, double seconds) {
    if (!app.active) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    app.world.update_entities(seconds, app.ui->editing().rotation_speed,
                              app.renderer->history_index(app.settings.render.version, app.paused));
    app.metrics.entity_milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
bool window_ready(Application& app) {
    if (app.window->closed()) {
        return false;
    }
    const auto extent = app.window->framebuffer();
    if (app.window->minimized() || extent.width == 0 || extent.height == 0) {
        app.window->wait();
        app.rebase_clock = true;
        return false;
    }
    if (!app.router.poll_window(app.window->key(interaction::input::Key::f11), true)) {
        return true;
    }
    const auto result = app.window->toggle_borderless();
    app.metrics.status = result ? "Window mode changed" : result.error();
    const auto snapshot = app.window->input(app.ui->access());
    app.router.synchronize(snapshot);
    app.left_down = snapshot.focused && snapshot.pointer.left;
    app.left_pressed = false;
    app.left_released = false;
    app.free_edit = false;
    app.free_edit_gesture = false;
    app.brush_active = false;
    app.rebase_clock = true;
    return false;
}
} // namespace
void reset_camera(Application& app) {
    const auto size = app.world.size();
    const auto side = static_cast<double>(std::max({size.x, size.y, size.z, 16}));
    const auto actual_size = app.world.actual_size();
    auto pose = starting_pose(actual_size);
    if (app.options.camera_metres) {
        const auto& input = *app.options.camera_metres;
        pose = {{input[0] / app.options.voxel_metres, input[1] / app.options.voxel_metres,
                 input[2] / app.options.voxel_metres},
                input[3],
                input[4]};
    }
    auto camera = motion::camera::Camera::create(side, pose);
    if (!camera) {
        throw std::runtime_error(camera.error());
    }
    if (!app.options.camera_metres && !original_seed(actual_size)) {
        aim_at_model(*camera, actual_size);
    }
    auto configuration = camera->settings();
    configuration.fov_degrees = app.settings.general.fov;
    if (app.options.voxel_metres != 1) {
        configuration.speed = std::min(side / 20, 2.0 / app.options.voxel_metres);
    }
    require(camera->configure(configuration));
    app.camera = std::move(*camera);
    app.reset = true;
}
void reframe_camera(Application& app) {
    const auto size = app.world.size();
    const auto side = static_cast<double>(std::max({size.x, size.y, size.z, 16}));
    require(app.camera->reframe(side));
}
void initialize(Application& app) {
    load_settings(app);
    if (app.options.mode_set) {
        app.settings.render.version = app.options.mode;
    }
    auto window = platform::window::Window::create(
        {app.options.width, app.options.height, "Sandbox", !app.options.hidden});
    if (!window) {
        throw std::runtime_error(window.error());
    }
    app.window = std::make_unique<platform::window::Window>(std::move(*window));
    auto renderer = rendering::vulkan::Renderer::create(
        *app.window, {.shaders = app.options.shaders,
                      .validation = app.options.validation,
                      .chunk_layout = app.options.chunk_layout,
                      .defer_frame_wait = app.options.defer_frame_wait,
                      .offscreen_output = app.options.offscreen_output,
                      .application_name = "Sandbox"});
    if (!renderer) {
        throw std::runtime_error(renderer.error());
    }
    app.renderer = std::make_unique<rendering::vulkan::Renderer>(std::move(*renderer));
    auto source = app.options.scene.empty()
                      ? world::model::build_model({256, 256, 256}, {world::model::Material{}}, {})
                      : world::storage::load(app.options.scene);
    generate_world(app, std::move(source), {}, 4);
    reset_camera(app);
    app.ui = std::make_unique<gui::frontend::Frontend>(*app.window);
    update_metrics(app);
    const auto stats = app.renderer->statistics();
    const auto scene = app.options.scene.empty()
                           ? "Empty world"
                           : "Loaded " + path_to_utf8(app.options.scene.filename());
    app.metrics.status = scene + " | " + stats.device + " | " + app.metrics.chunk_storage;
    std::cout << app.metrics.status << " | voxel " << app.options.voxel_metres << " m\n"
              << std::flush;
}
void frame(Application& app, double seconds) {
    synchronize_capture(app);
    app.window->poll();
    if (!window_ready(app)) {
        return;
    }
    app.active = app.options.hidden || app.window->focused();
    if (app.active) {
        poll_import(app);
    }
    const auto ready = app.renderer->begin_frame();
    if (!ready) {
        throw std::runtime_error(ready.error());
    }
    if (!*ready) {
        app.router.rebase_pointer();
        app.rebase_clock = true;
        return;
    }
    update_metrics(app);
    if (app.active) {
        app.camera->clear_changed();
    }
    const auto previous_path = app.settings.render.path_tracer;
    const auto requests =
        app.ui->draw({app.world, *app.camera, app.path, app.settings, app.metrics});
    handle_requests(app, requests);
    update_camera_settings(app, previous_path);
    auto snapshot = app.window->input();
    update_motion(app, snapshot, seconds);
    advance_entities(app, seconds);
    render(app, seconds);
}
int run(Options options) {
    Application app;
    app.options = std::move(options);
    initialize(app);
    auto previous = std::chrono::steady_clock::now();
    while (!app.window->closed() && (app.options.frames == 0 || app.frame < app.options.frames)) {
        const auto begin = std::chrono::steady_clock::now();
        const auto seconds = std::chrono::duration<double>(begin - previous).count();
        previous = begin;
        frame(app, seconds);
        if (app.rebase_clock) {
            previous = std::chrono::steady_clock::now();
            app.rebase_clock = false;
        }
        if (app.settings.general.lock_to_60) {
            std::this_thread::sleep_until(begin + std::chrono::microseconds(16667));
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                .count();
        app.metrics.milliseconds += (elapsed - app.metrics.milliseconds) * 0.1;
    }
    const auto stats = app.renderer->statistics();
    std::cout << "Rendered " << app.frame
              << " frames, validation errors: " << stats.validation_errors << '\n';
    return stats.validation_errors == 0 ? 0 : 2;
}
} // namespace engine::app::sandbox
