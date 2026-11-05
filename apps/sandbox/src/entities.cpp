#include "application.hpp"
#include <algorithm>
#include <exception>
#include <stdexcept>
namespace engine::app::sandbox {
namespace {
foundation::geometry::Vec3 bounded_grid(foundation::geometry::Vec3 origin,
                                        foundation::geometry::Vec3 half) {
    const foundation::geometry::Vec3 limit{16384 - 1.0 / 128, 4096 - 1.0 / 128, 16384 - 1.0 / 128};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto maximum = limit[axis] - half[axis] * 2;
        if (maximum < 0) {
            throw std::runtime_error(
                "Entity grid exceeds the packed coordinate range; reduce Amount to spawn.");
        }
        origin[axis] = std::clamp(origin[axis], 0.0, maximum);
    }
    return origin;
}
void move_grid(Application& app) {
    const auto& editing = app.ui->editing();
    const auto& type = app.world.entity_types()[editing.spawn_type];
    const auto spacing = 1.5 * std::max({type.size.x, type.size.y, type.size.z});
    const auto count = editing.spawn_amount;
    const foundation::geometry::Vec3 extent{spacing * count[0], spacing * count[1],
                                            spacing * count[2]};
    const auto distance = foundation::geometry::length(extent) * 0.5 + 20;
    const foundation::geometry::Vec3 half{(extent.x - spacing) * 0.5, (extent.y - spacing) * 0.5,
                                          (extent.z - spacing) * 0.5};
    const auto start =
        bounded_grid(app.camera->pose().position + app.pointer_ray * distance - half, half);
    const auto amount = static_cast<std::size_t>(count[0] * count[1] * count[2]);
    for (std::size_t index = 0; index < amount; ++index) {
        const auto z = index % static_cast<std::size_t>(count[2]);
        const auto y =
            index / static_cast<std::size_t>(count[2]) % static_cast<std::size_t>(count[1]);
        const auto x = index / static_cast<std::size_t>(count[2] * count[1]);
        const foundation::geometry::Vec3 offset{static_cast<double>(x) * spacing,
                                                static_cast<double>(y) * spacing,
                                                static_cast<double>(z) * spacing};
        app.world.set_entity_position(editing.spawn_begin + index, start + offset);
    }
}
} // namespace
void update_entities(Application& app) {
    auto& editing = app.ui->editing();
    if (editing.spawning) {
        try {
            move_grid(app);
        } catch (const std::exception& error) {
            app.metrics.status = error.what();
            app.world.remove_entities_from(editing.spawn_begin);
            editing.spawning = false;
        }
        if (app.left_pressed && app.pointer_available) {
            editing.spawning = false;
        }
    }
    app.metrics.entity_kilobytes =
        static_cast<double>(app.renderer->statistics().entity_upload_bytes) / 1000;
}
} // namespace engine::app::sandbox
