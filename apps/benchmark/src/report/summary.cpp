#include "../scenario/json.hpp"
#include "report.hpp"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
namespace engine::app::benchmark {
namespace {
std::string utc_now() {
    const auto time = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}
json::object timings(const Measurements& results) {
    std::vector<double> wall;
    std::vector<double> gpu;
    std::vector<double> edits;
    for (const auto& sample : results.frames) {
        wall.push_back(sample.wall_ms);
        gpu.push_back(sample.gpu_ms.value());
    }
    for (const auto& sample : results.events) {
        edits.push_back(sample.wall_ms);
    }
    return {{"frame_wall_ms", distribution(wall)},
            {"gpu_ms", distribution(gpu)},
            {"edit_event_wall_ms", distribution(edits)},
            {"scene_load_ms", results.scene_load_ms},
            {"world_generation_ms", results.generation_ms},
            {"setup_ms", results.setup_ms},
            {"warmup_wall_ms", results.warmup_ms},
            {"measurement_wall_ms", results.measurement_ms},
            {"final_drain_ms", results.final_drain_ms},
            {"observation_ms", results.observation_ms},
            {"throughput_fps",
             static_cast<double>(results.frames.size()) * 1000 / results.measurement_ms}};
}
void peak(json::value& target, const json::value& value) {
    if (!value.is_null()) {
        target = target.is_null() ? value
                                  : json::value(std::max(target.to_number<std::uint64_t>(),
                                                         value.to_number<std::uint64_t>()));
    }
}
json::value device_usage(const json::object& sample) {
    if (!sample.at("heap_budget_supported").as_bool()) {
        return nullptr;
    }
    std::uint64_t usage = 0;
    for (const auto& value : sample.at("heaps").as_array()) {
        const auto& heap = value.as_object();
        if (heap.at("device_local").as_bool()) {
            usage += heap.at("estimated_usage_bytes").to_number<std::uint64_t>();
        }
    }
    return usage;
}
json::object memory_summary(const Measurements& results) {
    json::value resident = nullptr;
    json::value lifetime = nullptr;
    json::value committed = nullptr;
    json::value device_local = nullptr;
    for (const auto& value : results.memory) {
        const auto& sample = value.as_object();
        peak(resident, sample.at("resident_bytes"));
        peak(lifetime, sample.at("lifetime_peak_resident_bytes"));
        peak(committed, sample.at("private_committed_bytes"));
        peak(device_local, device_usage(sample));
    }
    std::uint64_t renderer = 0;
    for (const auto& frame : results.frames) {
        renderer = std::max(renderer, frame.renderer_bytes);
    }
    return {{"sampled_peak_resident_bytes", resident},
            {"lifetime_peak_resident_bytes", lifetime},
            {"frame_boundary_peak_renderer_bytes", renderer},
            {"sampled_peak_private_committed_bytes", committed},
            {"sampled_peak_device_local_estimated_bytes", device_local},
            {"frame_record_storage_bytes", results.frames.capacity() * sizeof(FrameSample)}};
}
} // namespace
json::object summary(const Session& session) {
    const auto& results = session.results;
    return {
        {"schema", "engine.benchmark.run.v1"},
        {"status", "complete"},
        {"utc", utc_now()},
        {"name", session.scenario.name},
        {"label", session.options.label},
        {"run_index", session.options.run_index},
        {"process_id", platform::process::identity().process_id},
        {"kind", session.options.validation ? "validation" : "timing"},
        {"workload_sha256", fingerprint(results.contract)},
        {"workload", results.contract},
        {"environment", results.environment},
        {"build", results.build},
        {"assets", results.assets},
        {"input_scenario", session.scenario.document},
        {"timings", timings(results)},
        {"memory", memory_summary(results)},
        {"coverage", json::object{{"expected_frames", session.scenario.frames},
                                  {"recorded_frames", results.frames.size()},
                                  {"gpu_timed_frames", results.frames.size()},
                                  {"expected_events", session.scenario.events.size()},
                                  {"recorded_events", results.events.size()},
                                  {"prepared_entity_types", session.world.entity_types().size()},
                                  {"validated_probes", session.scenario.probes.size()}}},
        {"validation_enabled", session.options.validation},
        {"validation_errors", session.renderer->statistics().validation_errors},
        {"capture_after_measurement", session.options.capture}};
}
void finish_report(Session& session) {
    if (session.results.events.size() != session.scenario.events.size()) {
        throw std::runtime_error("Not all scripted events executed");
    }
    auto result = summary(session);
    result["artifacts"] = write_samples(session);
    write_json(session.options.output / "summary.json", result);
}
} // namespace engine::app::benchmark
