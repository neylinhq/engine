#include "../scenario/json.hpp"
#include "session.hpp"
#include <algorithm>
#include <bit>
#include <engine/rendering/configuration/configuration.hpp>
namespace engine::app::benchmark {
namespace {
json::array position(foundation::geometry::Vec3 value) {
    return {value.x, value.y, value.z};
}
json::object shaders(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto extension = entry.path().extension();
        if (entry.is_regular_file() && (extension == ".spv" || extension == ".json")) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    json::array records;
    for (const auto& file : files) {
        records.push_back(json::object{{"path", file.lexically_relative(root).generic_string()},
                                       {"sha256", platform::process::file_sha256(file)}});
    }
    if (records.empty()) {
        throw std::runtime_error("No compiled shaders found");
    }
    return {{"root", platform::process::path_text(root)},
            {"sha256", fingerprint(records)},
            {"files", std::move(records)}};
}
json::object environment(const Session& session) {
    const auto host = platform::process::identity();
    const auto gpu = session.renderer->device_info();
    const auto actual = session.renderer->statistics();
    return {{"system", host.system},
            {"processor", host.processor},
            {"hardware_threads", host.hardware_threads},
            {"gpu_name", gpu.name},
            {"gpu_uuid", gpu.uuid},
            {"driver_name", gpu.driver_name},
            {"driver_info", gpu.driver_info},
            {"vendor_id", gpu.vendor_id},
            {"device_id", gpu.device_id},
            {"driver_version_raw", gpu.driver_version},
            {"api_version_raw", gpu.api_version},
            {"queue_family", gpu.queue_family},
            {"timestamp_bits", gpu.timestamp_bits},
            {"timestamp_period_ns", gpu.timestamp_period_nanoseconds},
            {"actual_presentation", static_cast<int>(actual.presentation_mode)},
            {"actual_chunk_layout", static_cast<int>(actual.chunk_layout)},
            {"actual_direct_output", actual.direct_output},
            {"gpu_clock_policy", "driver-managed; no clock or power changes"},
            {"filesystem_cache_policy", "uncontrolled"}};
}
json::array palette(std::span<const world::model::Material> materials) {
    json::array result;
    for (const auto& material : materials) {
        result.push_back(json::object{{"name", material.name},
                                      {"color_base", position(material.color_base)},
                                      {"color_layer", position(material.color_layer)},
                                      {"base", static_cast<int>(material.base)},
                                      {"layer", static_cast<int>(material.layer)},
                                      {"roughness", material.roughness}});
    }
    return result;
}
json::array event_contract(const Session& session) {
    const auto& scenario = session.scenario;
    auto events = scenario.document.if_contains("events")
                      ? scenario.document.at("events").as_array()
                      : json::array{};
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto& event = scenario.events[index];
        if (!event.model.empty()) {
            auto& object = events[index].as_object();
            object.erase("model");
            const auto model_index = session.models.at(event.model);
            object["model_sha256"] = session.results.assets.at("models")
                                         .as_array()
                                         .at(model_index)
                                         .as_object()
                                         .at("sha256");
        }
    }
    return events;
}
json::array camera_contract(const Scenario& scenario) {
    json::array camera;
    for (const auto& key : scenario.camera) {
        camera.push_back(json::object{{"frame", key.frame},
                                      {"position", position(key.pose.position)},
                                      {"yaw", key.pose.yaw},
                                      {"pitch", key.pose.pitch}});
    }
    return camera;
}
json::object contract(const Session& session) {
    const auto& scenario = session.scenario;
    return {{"schema", "engine.benchmark.workload.v1"},
            {"scene_sha256", session.results.assets.at("scene").as_object().at("sha256")},
            {"world_palette", palette(session.world.materials())},
            {"voxel_size", scenario.voxel_size},
            {"frames", scenario.frames},
            {"warmup_frames", scenario.warmup_frames},
            {"warmup_policy", "static-initial-camera-no-events"},
            {"entity_policy", "types-prepared-before-warmup"},
            {"step_seconds", scenario.step_seconds},
            {"memory_interval", scenario.memory_interval},
            {"bounds_budget", scenario.bounds_budget},
            {"width", scenario.width},
            {"height", scenario.height},
            {"ui", false},
            {"hidden", session.options.hidden},
            {"borderless", scenario.borderless},
            {"validation", session.options.validation},
            {"presentation", static_cast<int>(scenario.renderer.presentation_mode)},
            {"profile_updates", scenario.renderer.profile_world_updates},
            {"profile_passes", scenario.renderer.profile_render_passes},
            {"offscreen", scenario.renderer.offscreen_output},
            {"settings", json::parse(rendering::configuration::serialize(scenario.settings))},
            {"camera", camera_contract(scenario)},
            {"events", event_contract(session)}};
}
} // namespace
json::object model_identity(const world::model::Model& model) {
    json::object result{
        {"size", json::array{model.size.x, model.size.y, model.size.z}},
        {"palette", palette(model.materials)},
        {"word_endian", std::endian::native == std::endian::little ? "little" : "big"},
        {"chunks_sha256", platform::process::sha256(std::as_bytes(std::span(model.chunks)))},
        {"blocks_sha256", platform::process::sha256(std::as_bytes(std::span(model.blocks)))},
        {"voxels_sha256", platform::process::sha256(std::as_bytes(std::span(model.voxels)))}};
    result["sha256"] = fingerprint(result);
    return result;
}
void identify(Session& session) {
    auto& results = session.results;
    results.contract = contract(session);
    results.environment = environment(session);
    const auto executable = platform::process::executable();
    results.build = {{"executable", platform::process::path_text(executable)},
                     {"executable_sha256", platform::process::file_sha256(executable)},
                     {"compiler", ENGINE_BENCHMARK_COMPILER},
                     {"configuration", ENGINE_BENCHMARK_BUILD_TYPE},
                     {"engine_version", ENGINE_BENCHMARK_VERSION},
                     {"shaders", shaders(session.scenario.renderer.shaders)}};
}
} // namespace engine::app::benchmark
