#include "json.hpp"
#include <engine/rendering/configuration/configuration.hpp>
#include <stdexcept>
namespace engine::app::benchmark {
namespace {
void render_settings(const json::object& object, Scenario& result) {
    if (const auto* value = object.if_contains("settings")) {
        result.settings = rendering::configuration::parse(json::serialize(*value), true);
    }
    if (const auto* value = object.if_contains("fov")) {
        result.settings.general.fov = static_cast<float>(real(*value, 1, 120));
    }
    if (const auto* value = object.if_contains("bounces")) {
        const auto count = static_cast<int>(integer(*value, 0, 10));
        result.settings.render.base.bounce_count = count;
        result.settings.render.path_tracer.bounce_count = count;
    }
    if (const auto* value = object.if_contains("mode")) {
        const auto mode = text(*value);
        using rendering::settings::RenderVersion;
        if (mode == "base") {
            result.settings.render.version = RenderVersion::base;
        } else if (mode == "albedo") {
            result.settings.render.version = RenderVersion::albedo;
        } else if (mode == "path_tracer") {
            result.settings.render.version = RenderVersion::path_tracer;
        } else {
            throw std::invalid_argument("Unknown render mode");
        }
    }
    if (result.settings.general.lock_to_60) {
        throw std::invalid_argument("Use presentation mode to select synchronization; lockTo60fps "
                                    "is not a benchmark throttle");
    }
}
void renderer_options(const json::object& object, Scenario& result) {
    result.renderer.presentation_mode = rendering::vulkan::PresentationMode::immediate;
    if (const auto* value = object.if_contains("presentation")) {
        const auto mode = text(*value);
        using rendering::vulkan::PresentationMode;
        if (mode == "immediate") {
            result.renderer.presentation_mode = PresentationMode::immediate;
        } else if (mode == "mailbox") {
            result.renderer.presentation_mode = PresentationMode::mailbox;
        } else if (mode == "fifo") {
            result.renderer.presentation_mode = PresentationMode::fifo;
        } else {
            throw std::invalid_argument("Unknown presentation mode");
        }
    }
    if (const auto* value = object.if_contains("profile")) {
        const auto profile = text(*value);
        if (profile != "frame" && profile != "updates" && profile != "passes") {
            throw std::invalid_argument("Unknown timestamp profile");
        }
        result.renderer.profile_world_updates = profile != "frame";
        result.renderer.profile_render_passes = profile == "passes";
    }
    if (const auto* value = object.if_contains("offscreen")) {
        result.renderer.offscreen_output = value->as_bool();
    }
}
void render(const json::object& object, Scenario& result) {
    keys(object, {"width", "height", "settings", "fov", "bounces", "mode", "presentation",
                  "profile", "offscreen", "borderless"});
    result.width = integer(object.at("width"), 1, 16384);
    result.height = integer(object.at("height"), 1, 16384);
    if (const auto* value = object.if_contains("borderless")) {
        result.borderless = value->as_bool();
    }
    render_settings(object, result);
    renderer_options(object, result);
    result.renderer.application_name = "Benchmark";
}
void scene(const json::object& object, Scenario& result, const std::filesystem::path& directory) {
    keys(object, {"file", "size"});
    if (object.contains("file") == object.contains("size")) {
        throw std::invalid_argument("Scene requires exactly one of file or empty size");
    }
    if (const auto* value = object.if_contains("file")) {
        result.scene = relative_file(*value, directory);
        return;
    }
    result.empty_size = voxel(object.at("size"));
    for (const auto axis : {result.empty_size.x, result.empty_size.y, result.empty_size.z}) {
        if (axis < 16 || axis > 16384 || axis % 16 != 0) {
            throw std::invalid_argument("Empty scene axes must be multiples of 16 in [16,16384]");
        }
    }
}
std::vector<Probe> probes(const json::array& values) {
    std::vector<Probe> result;
    for (const auto& value : values) {
        const auto& object = value.as_object();
        keys(object, {"position", "material"});
        result.push_back({voxel(object.at("position")),
                          static_cast<std::uint16_t>(integer(object.at("material"), 0, 65535))});
    }
    return result;
}
} // namespace
Scenario parse_scenario(const json::object& root, const std::filesystem::path& directory) {
    keys(root, {"schema", "name", "scene", "voxel_size", "render", "frames", "warmup_frames",
                "step_seconds", "memory_interval", "bounds_budget", "camera", "events", "probes"});
    if (text(root.at("schema")) != "engine.benchmark.scenario.v1") {
        throw std::invalid_argument("Unsupported scenario schema");
    }
    Scenario result;
    result.document = root;
    result.name = text(root.at("name"));
    result.frames = integer(root.at("frames"), 1, 10000000);
    result.warmup_frames = integer(root.at("warmup_frames"), 0, 10000000);
    result.voxel_size = real(root.at("voxel_size"), 1e-6, 1e6);
    result.step_seconds = real(root.at("step_seconds"), 1e-6, 1);
    result.memory_interval = integer(root.at("memory_interval"), 1, 10000000);
    if (const auto* value = root.if_contains("bounds_budget")) {
        result.bounds_budget = integer(*value, 0, 1000000000);
    }
    scene(root.at("scene").as_object(), result, directory);
    render(root.at("render").as_object(), result);
    result.camera = parse_camera(root.at("camera").as_array(), result.frames);
    if (const auto* value = root.if_contains("events")) {
        result.events = parse_events(value->as_array(), directory, result.frames);
    }
    if (const auto* value = root.if_contains("probes")) {
        result.probes = probes(value->as_array());
    }
    return result;
}
Scenario load_scenario(const Options& options) {
    const auto path = std::filesystem::absolute(options.scenario);
    auto result = parse_scenario(json::parse(read_text(path)).as_object(), path.parent_path());
    if (!options.scene.empty()) {
        result.scene = std::filesystem::absolute(options.scene);
    }
    result.renderer.shaders = options.shaders;
    result.renderer.validation = options.validation;
    std::vector<std::filesystem::path> assets;
    if (!result.scene.empty()) {
        assets.push_back(result.scene);
    }
    for (const auto& event : result.events) {
        if (!event.model.empty()) {
            assets.push_back(event.model);
        }
    }
    for (const auto& asset : assets) {
        if (!std::filesystem::is_regular_file(asset)) {
            throw std::invalid_argument("Scene/model does not exist: " + asset.string());
        }
    }
    return result;
}
} // namespace engine::app::benchmark
