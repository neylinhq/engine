#include "../../src/application.hpp"

#include <array>
#include <chrono>
#include <engine/gui/frontend/frontend.hpp>
#include <engine/rendering/settings/settings.hpp>
#include <engine/world/model/types.hpp>
#include <engine/world/storage/storage.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

namespace sandbox = engine::app::sandbox;
namespace gui = engine::gui::frontend;
namespace model = engine::world::model;
namespace storage = engine::world::storage;
namespace fs = std::filesystem;

constexpr model::Vec3i kVoxel{8, 8, 8};
constexpr model::Vec3i kOtherVoxel{24, 8, 8};
const model::Material kEmbedded{"stone", {1, 0, 0}};
const model::Material kRecolored{"stone", {0, 1, 0}};
const model::Material kRenamed{"grass", {0, 1, 0}};
const model::Material kLibrary{"stone", {0, 0, 1}};
const model::Material kPaint{"paint", {0, 0, 1}};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void request(sandbox::Application& app, gui::Operation operation, const fs::path& path) {
    gui::Request item;
    item.operation = operation;
    item.path = path;
    sandbox::handle_requests(app, {item});
}

void wait_for_import(sandbox::Application& app) {
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (app.importing && std::chrono::steady_clock::now() < timeout) {
        sandbox::poll_import(app);
        if (app.importing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    require(!app.importing, "Timed out importing a CVox");
}

void open(sandbox::Application& app, const fs::path& scene) {
    request(app, gui::Operation::open_world, scene);
    wait_for_import(app);
    require(app.metrics.status.starts_with("Loaded "), app.metrics.status);
    require(app.scene_source.world == scene, "Sandbox did not open the requested CVox");
    require(app.scene_source.materials.empty(), "Opening the CVox retained the old palette path");
}

void check_palette(sandbox::Application& app, const fs::path& file,
                   const model::Material& material) {
    const auto id = app.world.at(kVoxel);
    const auto geometry = app.world.view().geometry_revision;
    request(app, gui::Operation::load_materials, file);
    require(app.metrics.status.starts_with("Loaded materials "), app.metrics.status);
    require(app.scene_source.materials == file && app.world.materials()[id] == material,
            "Sandbox did not apply the external palette to its local ID");
    require(app.world.at(kVoxel) == id && app.world.view().geometry_revision == geometry,
            "Loading the palette changed voxel IDs or geometry");
}

void test_reopen(sandbox::Application& app, const fs::path& file, const model::Material& material) {
    check_palette(app, file, material);
    open(app, app.options.scene);
    require(app.world.materials()[app.world.at(kVoxel)] == kEmbedded,
            "Reopened CVox did not restore its embedded material");
    check_palette(app, file, material);
    require(app.world.models().size() == 1, "Reopening lost the model library");
}

void test_startup(sandbox::Application& app, const fs::path& renamed) {
    app.options.materials = renamed;
    sandbox::initialize(app);
    require(app.world.materials()[app.world.at(kVoxel)] == kRenamed,
            "Sandbox startup did not apply the external palette");
    open(app, app.options.scene);
    require(app.world.materials()[app.world.at(kVoxel)] == kEmbedded,
            "Opening CVox retained the startup palette");
    check_palette(app, renamed, kRenamed);
}

void test_runtime(const sandbox::Options& options, const fs::path& recolored,
                  const fs::path& renamed) {
    sandbox::Application app;
    app.options = options;
    const auto library =
        app.world.add_model(model::build_model({1, 1, 1}, {{}, kLibrary}, {{{0, 0, 0}, 1}}));
    const auto library_id = model::voxel_at(app.world.models()[library], {});
    sandbox::initialize(app);
    require(app.world.materials()[app.world.at(kVoxel)] == kEmbedded,
            "An imported model overrode the scene's embedded material");
    test_reopen(app, recolored, kRecolored);
    test_reopen(app, renamed, kRenamed);
    auto edited = app.world.materials()[app.world.at(kVoxel)];
    edited.color_base = {1, 1, 0};
    app.world.set_material(app.world.at(kVoxel), edited);
    const auto library_scene = app.options.scene.parent_path() / "library.cvox";
    storage::save_cvox(library_scene, model::build_model({1, 1, 1}, {{}}, {}));
    open(app, library_scene);
    require(app.world.materials()[library_id] == kLibrary,
            "Palette load or a scene edit changed a shared material in the model library");
    sandbox::frame(app, 0.016);
    const auto finished = app.renderer->finish();
    if (!finished) {
        throw std::runtime_error(finished.error());
    }
    require(app.renderer->statistics().validation_errors == 0, "Vulkan validation failed");
}

void test_legacy_palette(sandbox::Options options, const fs::path& scene) {
    options.materials = options.scene.parent_path() / "legacy.materials.json";
    options.scene = scene;
    storage::save_materials(options.materials,
                            std::array<model::Material, 3>{{{}, kEmbedded, kRenamed}});
    const auto embedded = storage::load_cvox(scene);
    require(embedded.materials[1].name == embedded.materials[2].name,
            "Fixture must have separate IDs with the same embedded name");
    sandbox::Application app;
    app.options = options;
    sandbox::initialize(app);
    const auto stone = app.world.at(kVoxel);
    const auto grass = app.world.at(kOtherVoxel);
    require(stone != grass && app.world.materials()[stone] == kEmbedded &&
                app.world.materials()[grass] == kRenamed,
            "Startup palette lost a legacy material ID");
    open(app, scene);
    const auto first = app.world.at(kVoxel);
    const auto second = app.world.at(kOtherVoxel);
    require(first != second && app.world.materials()[first] == embedded.materials[1] &&
                app.world.materials()[second] == embedded.materials[2],
            "Opening the embedded palette changed its IDs, names or properties");
    const auto exported = options.materials.parent_path() / "legacy_export.materials.json";
    request(app, gui::Operation::save_materials, exported);
    require(app.metrics.status.starts_with("Saved materials "), app.metrics.status);
    require(storage::load_materials(exported, embedded.materials.size()) == embedded.materials,
            "Export did not preserve the repeated display names of the CVox palette");
    check_palette(app, options.materials, kEmbedded);
    require(app.world.at(kOtherVoxel) == second && app.world.materials()[second] == kRenamed,
            "Live palette could not address the second legacy material");

    request(app, gui::Operation::import_model, scene);
    wait_for_import(app);
    require(app.metrics.status.starts_with("Imported model "), app.metrics.status);
    const auto& imported = app.world.models().back();
    const auto red = model::voxel_at(imported, kVoxel);
    const auto blue = model::voxel_at(imported, kOtherVoxel);
    require(red != blue && app.world.materials()[red] == embedded.materials[1] &&
                app.world.materials()[blue] == embedded.materials[2],
            "Importing a legacy model changed its same-named materials");
}

void test_previous_scene(const sandbox::Options& options, const fs::path& renamed) {
    const auto first = options.scene.parent_path() / "grass.cvox";
    storage::save_cvox(first, model::build_model({16, 16, 16}, {{}, kRenamed}, {{kVoxel, 1}}));
    sandbox::Application app;
    app.options = options;
    app.options.scene = first;
    sandbox::initialize(app);
    require(app.world.models().empty(), "Fixture requires an empty model library");
    const auto palette_size = app.world.materials().size();
    open(app, options.scene);
    check_palette(app, renamed, kRenamed);
    open(app, first);
    require(app.world.materials().size() == palette_size,
            "Switching scenes accumulated material entries");
}

void test_reserved_material(sandbox::Options options) {
    const auto directory = options.scene.parent_path();
    options.scene = directory / "reserved.cvox";
    options.materials = directory / "reserved.materials.json";
    const model::Material embedded_zero{"_", {0.125, 0.25, 0.5}};
    const model::Material external_zero{"air", {0.25, 0.5, 0.75}};
    storage::save_cvox(options.scene,
                       model::build_model({16, 16, 16}, {embedded_zero, kEmbedded}, {{kVoxel, 1}}));
    storage::save_materials(options.materials,
                            std::array<model::Material, 2>{external_zero, kRecolored});

    sandbox::Application app;
    app.options = options;
    sandbox::initialize(app);
    const auto exported = directory / "reserved_export.materials.json";
    request(app, gui::Operation::save_materials, exported);
    require(storage::load_materials(exported, 2)[0] == external_zero,
            "Export dropped the external reserved entry");
    open(app, options.scene);
    request(app, gui::Operation::save_materials, exported);
    require(storage::load_materials(exported, 2)[0] == embedded_zero,
            "Reopening did not restore the embedded reserved entry");
    request(app, gui::Operation::load_materials, options.materials);
    const auto saved = directory / "reserved_export.cvox";
    request(app, gui::Operation::save_world, saved);
    require(storage::load_cvox(saved).materials[0].color_base == external_zero.color_base,
            "CVox Save As lost the properties of the reserved entry");
    request(app, gui::Operation::save_materials, exported);
    require(storage::load_materials(exported, 2)[0] == external_zero &&
                app.world.at(kOtherVoxel) == 0 && app.world.materials()[0] == model::Material{},
            "The reserved entry changed after saving the scene or entered the runtime palette");
}

void test_compacted_palette(sandbox::Options options) {
    const auto directory = options.scene.parent_path();
    options.scene = directory / "unused.cvox";
    options.materials = directory / "unused.materials.json";
    auto embedded_paint = kPaint;
    embedded_paint.color_base = {1, 0, 0};
    storage::save_cvox(options.scene,
                       model::build_model({32, 16, 16}, {{}, kEmbedded, embedded_paint},
                                          {{kVoxel, 1}, {kOtherVoxel, 2}}));
    storage::save_materials(options.materials,
                            std::array<model::Material, 3>{{{}, kEmbedded, kPaint}});
    sandbox::Application app;
    app.options = options;
    sandbox::initialize(app);
    const auto paint = app.scene_source.material_ids[2];
    require(app.world.at(kOtherVoxel) == paint, "Expected a live voxel using the brush material");
    app.world.set(kOtherVoxel, 0);
    app.world.commit_edits();
    const auto saved = directory / "compacted.cvox";
    request(app, gui::Operation::save_world, saved);
    require(app.scene_source.world == saved && app.scene_source.material_ids.size() == 2,
            "Save As retained the erased material");
    const auto snapshot = storage::load_cvox(saved);
    require(snapshot.materials.size() == 2 && model::voxel_at(snapshot, kOtherVoxel) == 0 &&
                snapshot.blocks.size() == 128 && snapshot.voxels.size() == 64,
            "CVox retained unreachable block or voxel data");
    const auto palette = directory / "compacted.materials.json";
    request(app, gui::Operation::save_materials, palette);
    require(app.metrics.status.starts_with("Saved materials "), app.metrics.status);
    auto brush = kPaint;
    brush.color_base = {1, 1, 0};
    app.world.set_material(paint, brush);
    app.world.set(kVoxel, paint);
    app.world.commit_edits();
    request(app, gui::Operation::load_materials, palette);
    require(app.metrics.status.starts_with("Loaded materials "), app.metrics.status);
    require(app.world.at(kVoxel) == paint && app.world.materials()[paint] == brush,
            "Loading a compacted palette changed an unlisted material used by the brush");
}

std::uint16_t import_model(sandbox::Application& app, const fs::path& path) {
    request(app, gui::Operation::import_model, path);
    wait_for_import(app);
    require(app.metrics.status.starts_with("Imported model "), app.metrics.status);
    return model::voxel_at(app.world.models().back(), {});
}

void test_library_name_collision(const sandbox::Options& options, const fs::path& renamed) {
    const auto directory = options.scene.parent_path();
    const auto stone_model = directory / "stone_model.cvox";
    const auto grass_model = directory / "grass_model.cvox";
    const model::Material library_grass{"grass", {0, 0, 1}};
    storage::save_cvox(stone_model, model::build_model({1, 1, 1}, {{}, kLibrary}, {{{}, 1}}));
    storage::save_cvox(grass_model, model::build_model({1, 1, 1}, {{}, library_grass}, {{{}, 1}}));

    sandbox::Application app;
    app.options = options;
    sandbox::initialize(app);
    const auto scene_id = app.world.at(kVoxel);
    const auto stone = import_model(app, stone_model);
    const auto grass = import_model(app, grass_model);
    require(stone == scene_id && grass != scene_id, "Model bindings do not match the source names");

    check_palette(app, renamed, kRenamed);
    require(app.world.materials()[grass] == library_grass, "Scene palette changed a different ID");
    require(import_model(app, grass_model) == grass && import_model(app, stone_model) == stone,
            "Loading a new display name changed model bindings");

    const model::Vec3i stamped{7, 8, 8};
    app.world.set(stamped, grass);
    app.world.commit_edits();
    const auto saved = directory / "collision.cvox";
    const auto exported = directory / "collision.materials.json";
    request(app, gui::Operation::save_world, saved);
    require(app.metrics.status.starts_with("Saved "), app.metrics.status);
    request(app, gui::Operation::save_materials, exported);
    require(app.metrics.status.starts_with("Saved materials "), app.metrics.status);
    const auto result = storage::load_scene(saved, exported);
    const auto terrain_id = model::voxel_at(result.model, kVoxel);
    const auto stamped_id = model::voxel_at(result.model, stamped);
    require(terrain_id != stamped_id && result.model.materials[terrain_id].name == "stone" &&
                result.model.materials[stamped_id].name == "grass" &&
                result.palette[terrain_id] == kRenamed &&
                result.palette[stamped_id] == library_grass,
            "Save As merged different materials with the same display name");
    open(app, saved);
    request(app, gui::Operation::load_materials, exported);
    require(app.metrics.status.starts_with("Loaded materials "), app.metrics.status);
    require(app.world.at(kVoxel) == stone && app.world.at(stamped) == grass &&
                app.world.materials()[stone].color_base == kRenamed.color_base &&
                app.world.materials()[grass].color_base == library_grass.color_base,
            "Reopening the saved scene lost one of the materials");
    open(app, options.scene);
    require(app.world.materials()[grass] == library_grass &&
                app.world.materials()[stone] == kEmbedded,
            "Reopening the original scene changed the model library bindings");
}

void test_override_before_model_import(sandbox::Options options, const fs::path& renamed,
                                       bool startup) {
    if (startup) {
        options.materials = renamed;
    }
    sandbox::Application app;
    app.options = options;
    sandbox::initialize(app);
    const auto directory = options.scene.parent_path();
    const auto stone = import_model(app, directory / "stone_model.cvox");
    if (!startup) {
        check_palette(app, renamed, kRenamed);
    }
    const auto grass = import_model(app, directory / "grass_model.cvox");
    require(app.world.at(kVoxel) == stone && grass != stone &&
                app.world.materials()[stone] == kRenamed &&
                app.world.materials()[grass].name == "grass",
            "Renaming a shared material blocked or redirected the next model import");
}

void test_model_materials(const sandbox::Options& options) {
    const auto imported = options.scene.parent_path() / "model.cvox";
    storage::save_cvox(imported, model::build_model({1, 1, 1}, {{}, kLibrary}, {{{}, 1}}));
    sandbox::Application app;
    app.options = options;
    sandbox::initialize(app);
    const auto stone = app.world.at(kVoxel);
    request(app, gui::Operation::import_model, imported);
    wait_for_import(app);
    require(app.metrics.status.starts_with("Imported model "), app.metrics.status);
    const auto id = model::voxel_at(app.world.models().front(), {});
    require(id == stone && app.world.materials()[id] == kEmbedded,
            "Model import lost the shared material in the open scene");
    auto renamed = kEmbedded;
    renamed.name = "edited_stone";
    app.world.set_material(id, renamed);
    const auto saved = options.scene.parent_path() / "edited_stone.cvox";
    request(app, gui::Operation::save_world, saved);
    require(app.metrics.status.starts_with("Saved "), app.metrics.status);
    open(app, saved);
    require(app.world.at(kVoxel) == id && app.world.materials()[id] == renamed &&
                model::voxel_at(app.world.models().front(), {}) == id,
            "Reopening a manual rename lost the name or changed its model binding");

    open(app, options.scene.parent_path() / "library.cvox");
    auto baseline = kLibrary;
    baseline.name = renamed.name;
    require(app.world.has_material(id) && app.world.materials()[id] == baseline,
            "Manual rename failed to preserve the library's original material properties");
    const auto material = app.world.add_material(kPaint);
    require(app.world.has_material(material) && app.world.materials()[material] == kPaint,
            "Could not create a material after reclaiming scene IDs");
}

void test(const fs::path& shaders, const fs::path& directory, const fs::path& legacy_scene) {
    fs::create_directories(directory);
    const auto scene = directory / "scene.cvox";
    const auto recolored = directory / "recolored.materials.json";
    const auto renamed = directory / "renamed.materials.json";
    storage::save_cvox(scene, model::build_model({16, 16, 16}, {{}, kEmbedded}, {{kVoxel, 1}}));
    storage::save_materials(recolored, std::array<model::Material, 2>{{{}, kRecolored}});
    storage::save_materials(renamed, std::array<model::Material, 2>{{{}, kRenamed}});

    sandbox::Options options;
    options.scene = scene;
    options.shaders = shaders;
    options.width = 480;
    options.height = 320;
    options.hidden = true;
    options.no_ui = true;
    options.validation = true;
    options.mode = engine::rendering::settings::RenderVersion::albedo;
    test_runtime(options, recolored, renamed);
    test_model_materials(options);
    test_library_name_collision(options, renamed);
    test_override_before_model_import(options, renamed, false);
    test_override_before_model_import(options, renamed, true);
    test_previous_scene(options, renamed);
    test_legacy_palette(options, legacy_scene);
    test_reserved_material(options);
    test_compacted_palette(options);
    sandbox::Application app;
    app.options = options;
    test_startup(app, renamed);
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 4, "Expected shader directory, test output directory, and legacy CVox");
        test(argv[1], argv[2], argv[3]);
    } catch (const std::exception& error) {
        std::cerr << "Palette reopen test: " << error.what() << '\n';
        return 1;
    }
}
