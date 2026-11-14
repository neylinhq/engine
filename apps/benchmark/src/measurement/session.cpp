#include "session.hpp"
#include "../scenario/json.hpp"
#include <algorithm>
#include <engine/world/storage/storage.hpp>
#include <iostream>
#include <span>
#include <string_view>
namespace engine::app::benchmark {
namespace {
world::storage::SceneSource source_model(const Scenario& scenario) {
    if (!scenario.scene.empty()) {
        return world::storage::load_scene(scenario.scene, scenario.materials);
    }
    std::vector<world::model::Material> materials{{},
                                                  {.name = "red", .color_base = {0.8, 0.2, 0.1}},
                                                  {.name = "green", .color_base = {0.1, 0.8, 0.2}}};
    return {world::model::build_model(scenario.empty_size, std::move(materials), {}), {}};
}
json::object file_identity(const std::filesystem::path& path) {
    return {{"path", platform::process::path_text(path)},
            {"sha256", platform::process::file_sha256(path)},
            {"bytes", std::filesystem::file_size(path)}};
}
// Scenario IDs follow first registration, with separate entries for anonymous materials.
std::vector<std::string_view> material_keys(std::span<world::model::Material> materials,
                                            std::size_t& anonymous_id) {
    std::vector<std::string_view> keys;
    keys.reserve(materials.size());
    for (auto& material : materials) {
        if (material.name.empty() || material.name.starts_with('_')) {
            material.name = "_" + std::to_string(anonymous_id++);
            keys.emplace_back();
        } else {
            keys.emplace_back(material.name);
        }
    }
    return keys;
}
void generate_world(Session& session, world::storage::SceneSource source,
                    std::size_t& anonymous_id) {
    auto identity = model_identity(source.model, source.palette);
    if (!session.scenario.scene.empty()) {
        identity["file"] = file_identity(session.scenario.scene);
    }
    auto& assets = session.results.assets;
    assets["scene"] = std::move(identity);
    if (!session.scenario.materials.empty()) {
        assets["materials"] = {{"path", platform::process::path_text(session.scenario.materials)}};
    }
    if (!session.scenario.settings_file.empty()) {
        assets["settings"] = {
            {"path", platform::process::path_text(session.scenario.settings_file)}};
    }
    const auto generation = Clock::now();
    auto materials =
        source.palette.empty() ? std::span(source.model.materials) : std::span(source.palette);
    const auto keys = material_keys(materials, anonymous_id);
    auto mapped = session.world.prepare_generation(std::move(source.model), {source.palette, keys});
    take(session.renderer->generate_world(session.world, mapped, mapped.size, 4));
    session.results.generation_ms = elapsed(generation);
}
void load_models(Session& session, std::size_t& anonymous_id) {
    json::array models;
    for (const auto& event : session.scenario.events) {
        if (event.model.empty() || session.models.contains(event.model)) {
            continue;
        }
        auto source = world::storage::load(event.model);
        auto identity = model_identity(source);
        identity["file"] = file_identity(event.model);
        const auto keys = material_keys(source.materials, anonymous_id);
        const auto index = session.world.add_model(std::move(source), {{}, keys});
        session.models.emplace(event.model, index);
        identity["index"] = index;
        models.push_back(std::move(identity));
    }
    session.results.assets["models"] = std::move(models);
}
void camera(Session& session) {
    auto pose = session.scenario.camera.front().pose;
    pose.position = pose.position / session.scenario.voxel_size;
    const auto size = session.world.size();
    session.camera = take(motion::camera::Camera::create(std::max({size.x, size.y, size.z}), pose));
    auto settings = session.camera->settings();
    settings.fov_degrees = session.scenario.settings.general.fov;
    take(session.camera->configure(settings));
}
void prepare_entities(Session& session) {
    for (const auto& event : session.scenario.events) {
        if (event.operation == Operation::spawn && !session.entity_types.contains(event.model)) {
            const auto type = session.world.create_entity_type(session.models.at(event.model));
            session.entity_types.emplace(event.model, type);
        }
    }
}
void validate_materials(const Session& session) {
    for (const auto& event : session.scenario.events) {
        const bool brush = event.operation == Operation::brush && !event.brush.erase;
        const bool fill = event.operation == Operation::fill;
        const auto material = brush ? event.brush.material : event.material;
        if ((brush || fill) && !session.world.has_material(material)) {
            throw std::invalid_argument("Event material is outside the loaded world palette");
        }
    }
    const auto size = session.world.size();
    for (const auto& probe : session.scenario.probes) {
        if (probe.voxel.x < 0 || probe.voxel.y < 0 || probe.voxel.z < 0 ||
            probe.voxel.x >= size.x || probe.voxel.y >= size.y || probe.voxel.z >= size.z ||
            !session.world.has_material(probe.material)) {
            throw std::invalid_argument("Probe position or material is outside the loaded world");
        }
    }
}
} // namespace
void initialize(Session& session) {
    const auto begin = Clock::now();
    const auto& scenario = session.scenario;
    auto source = source_model(scenario);
    session.results.scene_load_ms = elapsed(begin);
    session.results.frames.resize(scenario.frames);
    session.results.events.reserve(scenario.events.size());
    session.results.memory.reserve(scenario.frames / scenario.memory_interval + 4);
    auto window = take(platform::window::Window::create({static_cast<int>(scenario.width),
                                                         static_cast<int>(scenario.height),
                                                         "Benchmark", !session.options.hidden}));
    session.window = std::make_unique<platform::window::Window>(std::move(window));
    if (scenario.borderless) {
        take(session.window->toggle_borderless());
    }
    auto renderer = take(rendering::vulkan::Renderer::create(*session.window, scenario.renderer));
    session.renderer = std::make_unique<rendering::vulkan::Renderer>(std::move(renderer));
    const auto status = session.renderer->statistics();
    if (!status.gpu_timestamps_supported) {
        throw std::runtime_error("GPU timestamps are unavailable on the selected queue");
    }
    if (status.width != scenario.width || status.height != scenario.height) {
        throw std::runtime_error("Requested render resolution " + std::to_string(scenario.width) +
                                 "x" + std::to_string(scenario.height) + ", actual " +
                                 std::to_string(status.width) + "x" +
                                 std::to_string(status.height));
    }
    std::size_t anonymous_id = 0;
    generate_world(session, std::move(source), anonymous_id);
    load_models(session, anonymous_id);
    prepare_entities(session);
    validate_materials(session);
    camera(session);
    identify(session);
    session.results.setup_ms = elapsed(begin);
    observe_memory(session, "loaded");
}
void verify_probes(const Session& session) {
    for (const auto& probe : session.scenario.probes) {
        if (session.world.at(probe.voxel) != probe.material) {
            throw std::runtime_error(
                "Final voxel probe failed at " + std::to_string(probe.voxel.x) + "," +
                std::to_string(probe.voxel.y) + "," + std::to_string(probe.voxel.z) +
                ": expected " + std::to_string(probe.material) + ", actual " +
                std::to_string(session.world.at(probe.voxel)));
        }
    }
}
int measure(const Options& options) {
    auto scenario = load_scenario(options);
    if (!options.output.parent_path().empty()) {
        std::filesystem::create_directories(options.output.parent_path());
    }
    if (!std::filesystem::create_directory(options.output)) {
        throw std::invalid_argument("Output directory must not already exist");
    }
    try {
        Session session;
        session.options = options;
        session.scenario = std::move(scenario);
        initialize(session);
        execute(session);
        verify_probes(session);
        finish_report(session);
        return 0;
    } catch (const std::exception& error) {
        write_json(options.output / "failure.json",
                   json::object{{"schema", "engine.benchmark.failure.v1"},
                                {"status", "failed"},
                                {"message", error.what()},
                                {"run_index", options.run_index}});
        std::cerr << "Benchmark worker failed: " << error.what() << '\n';
        return 3;
    }
}
} // namespace engine::app::benchmark
