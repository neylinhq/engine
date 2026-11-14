#include "../application.hpp"

#include <engine/world/storage/storage.hpp>
#include <utility>

namespace engine::app::sandbox {

void load_materials(Application& app, const std::filesystem::path& path) {
    auto source = path;
    const auto& ids = app.scene_source.material_ids;
    auto materials = world::storage::load_materials(source, ids.size());
    std::vector<world::model::MaterialUpdate> updates;
    updates.reserve(ids.size());
    for (std::size_t local_id = 0; local_id < ids.size(); ++local_id) {
        if (ids[local_id] != 0) {
            updates.push_back({ids[local_id], std::move(materials[local_id])});
        }
    }
    if (app.world.set_scene_materials(updates)) {
        app.renderer->reset_accumulation();
    }
    if (ids.front() == 0) {
        app.scene_source.reserved_material = std::move(materials.front());
    }
    app.scene_source.materials = std::move(source);
}

void save_materials(const Application& app, const std::filesystem::path& path) {
    auto palette = app.world.scene_palette(app.scene_source.material_ids);
    if (!palette.empty() && app.scene_source.material_ids.front() == 0) {
        palette.front() = app.scene_source.reserved_material;
    }
    world::storage::save_materials(path, palette);
}

} // namespace engine::app::sandbox
