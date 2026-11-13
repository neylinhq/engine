#include "application.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <engine/world/collision/placement.hpp>
#include <exception>
namespace engine::app::sandbox {
namespace {
world::model::Vec3i integer(foundation::geometry::Vec3 position) {
    return {static_cast<int>(std::floor(position.x)), static_cast<int>(std::floor(position.y)),
            static_cast<int>(std::floor(position.z))};
}
bool model_stamp(Application& app, const world::model::Hit& hit,
                 world::model::PlacementGuard guard) {
    const auto& editing = app.ui->editing();
    if (editing.stamp_model < 0 ||
        static_cast<std::size_t>(editing.stamp_model) >= app.world.models().size()) {
        return true;
    }
    auto position = app.camera->pose().position + app.pointer_ray * hit.distance + hit.normal * 0.5;
    if (editing.place_camera) {
        const auto camera = app.camera->pose().position;
        position = {std::floor(camera.x / 4) * 4, std::floor(camera.y / 4) * 4,
                    std::floor(camera.z / 4) * 4};
    }
    return world::editing::stamp_model(
        app.world, app.world.models()[static_cast<std::size_t>(editing.stamp_model)],
        integer(position), guard);
}
bool brush(Application& app, const world::model::Hit& hit, bool erase, double seconds,
           world::model::PlacementGuard guard) {
    const auto& editing = app.ui->editing();
    const auto target = app.camera->pose().position + app.pointer_ray * hit.distance;
    if (!app.brush_active) {
        app.brush_position = target;
    } else {
        const auto alpha = 1.0 - 1.0 / (1.0 + seconds * 150.0 / editing.radius);
        app.brush_position = foundation::geometry::interpolate(app.brush_position, target, alpha);
    }
    return world::editing::apply_brush(
        app.world, {editing.tool, app.brush_position, editing.radius, editing.material, erase},
        guard);
}
bool apply_tool(Application& app, const world::model::Hit& hit, double seconds,
                world::model::PlacementGuard guard) {
    const auto& editing = app.ui->editing();
    const auto shape =
        editing.tool == world::editing::Tool::sphere || editing.tool == world::editing::Tool::cube;
    const auto erase = shape && editing.erase;
    if (shape && !erase && editing.material == 0) {
        return true;
    }
    if (editing.tool == world::editing::Tool::model) {
        return model_stamp(app, hit, guard);
    }
    if (editing.tool == world::editing::Tool::flood_fill) {
        world::editing::flood_fill(app.world, hit.voxel, editing.material);
        return true;
    }
    return brush(app, hit, erase, seconds, guard);
}
void pick_material(Application& app) {
    if (!app.left_released || !app.pointer_available) {
        return;
    }
    const auto selected = app.world.trace(app.camera->pose().position, app.pointer_ray);
    if (selected.hit) {
        auto& editing = app.ui->editing();
        editing.material = selected.material;
        editing.focus_material = true;
        editing.pick_material = false;
    }
}
} // namespace
void update_editing(Application& app, const interaction::input::Actions& actions, double seconds) {
    auto& editing = app.ui->editing();
    if (actions.wheel != 0) {
        editing.radius =
            std::clamp(editing.radius + static_cast<float>(actions.wheel), 1.0F, 400.0F);
    }
    if (editing.pick_material) {
        pick_material(app);
        return;
    }
    const auto free_edit = app.free_edit && !app.router.state().captured;
    if (editing.spawning || (!actions.erase && !free_edit)) {
        app.brush_active = false;
        return;
    }
    const auto hit = app.world.trace(app.camera->pose().position, app.pointer_ray);
    if (editing.tool == world::editing::Tool::none || (!hit.hit && !editing.place_camera)) {
        return;
    }
    if (editing.material >= app.world.materials().size() && !editing.erase &&
        editing.tool != world::editing::Tool::model) {
        app.metrics.status = "Select a material in World > Editing before applying this tool.";
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    try {
        world::model::PlacementGuard guard{};
        world::collision::CapsulePlacement placement{};
        if (app.character) {
            placement = {app.character->capsule(), app.options.voxel_metres};
            guard = placement.guard();
        }
        app.brush_active = apply_tool(app, hit, seconds, guard);
        if (!app.brush_active) {
            app.metrics.status = "Placement blocked by the character";
        }
    } catch (const std::exception& error) {
        app.metrics.status = error.what();
    }
    app.metrics.editing_milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
} // namespace engine::app::sandbox
