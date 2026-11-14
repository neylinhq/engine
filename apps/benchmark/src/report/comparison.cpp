#include "../scenario/json.hpp"
#include "report.hpp"
#include <array>
#include <iostream>
#include <set>
namespace engine::app::benchmark {
namespace {
json::object checked_report(const std::filesystem::path& directory) {
    // Workload numbers must round-trip before the fingerprint is verified.
    auto report = json::parse(read_text(directory / "summary.json"), {},
                              {.numbers = json::number_precision::precise})
                      .as_object();
    if (report.at("schema") != "engine.benchmark.run.v1" || report.at("status") != "complete" ||
        report.at("validation_errors").to_number<unsigned>() != 0) {
        throw std::invalid_argument("Only complete, error-free benchmark reports can be compared");
    }
    const auto& coverage = report.at("coverage").as_object();
    if (coverage.at("expected_frames") != coverage.at("recorded_frames") ||
        coverage.at("expected_frames") != coverage.at("gpu_timed_frames") ||
        coverage.at("expected_events") != coverage.at("recorded_events") ||
        text(report.at("workload_sha256")) != fingerprint(report.at("workload"))) {
        throw std::invalid_argument("Incomplete or inconsistent benchmark coverage");
    }
    for (const auto& artifact : report.at("artifacts").as_object()) {
        const auto name = std::string(artifact.key());
        if (std::filesystem::path(name).filename() != name ||
            platform::process::file_sha256(directory / name) != text(artifact.value())) {
            throw std::invalid_argument("Sample artifact integrity mismatch");
        }
    }
    return report;
}
json::object environment_key(const json::object& report) {
    const auto& environment = report.at("environment").as_object();
    json::object result;
    for (const auto key : {"system", "processor", "hardware_threads", "gpu_uuid",
                           "driver_version_raw", "driver_name", "driver_info", "api_version_raw"}) {
        result[key] = environment.at(key);
    }
    result["configuration"] = report.at("build").as_object().at("configuration");
    return result;
}
void compatible(const json::object& baseline, const json::object& candidate) {
    if (baseline.at("workload_sha256") != candidate.at("workload_sha256")) {
        throw std::invalid_argument(
            "Incompatible workload: scenario, inputs, settings or measurement controls differ");
    }
    if (environment_key(baseline) != environment_key(candidate)) {
        throw std::invalid_argument(
            "Incompatible environment: CPU, GPU, driver, OS or build configuration differ");
    }
}
struct Metric {
    std::string_view name;
    std::string_view section = "timings";
    std::string_view field;
    std::string_view statistic;
};
auto metrics() {
    return std::array{
        Metric{"frame_wall_ms", "timings", "frame_wall_ms", "mean"},
        Metric{"gpu_ms", "timings", "gpu_ms", "mean"},
        Metric{"frame_wall_p95_ms", "timings", "frame_wall_ms", "p95"},
        Metric{"frame_wall_p99_ms", "timings", "frame_wall_ms", "p99"},
        Metric{"gpu_p99_ms", "timings", "gpu_ms", "p99"},
        Metric{"edit_event_wall_ms", "timings", "edit_event_wall_ms", "mean"},
        Metric{"scene_load_ms", "timings", "scene_load_ms", {}},
        Metric{"world_generation_ms", "timings", "world_generation_ms", {}},
        Metric{"measurement_wall_ms", "timings", "measurement_wall_ms", {}},
        Metric{"throughput_fps", "timings", "throughput_fps", {}},
        Metric{"sampled_peak_resident_bytes", "memory", "sampled_peak_resident_bytes", {}},
        Metric{"lifetime_peak_resident_bytes", "memory", "lifetime_peak_resident_bytes", {}},
        Metric{"sampled_peak_private_committed_bytes",
               "memory",
               "sampled_peak_private_committed_bytes",
               {}},
        Metric{"frame_boundary_peak_renderer_bytes",
               "memory",
               "frame_boundary_peak_renderer_bytes",
               {}},
        Metric{"sampled_peak_device_local_estimated_bytes",
               "memory",
               "sampled_peak_device_local_estimated_bytes",
               {}}};
}
std::vector<double> values(const json::array& reports, const Metric& metric) {
    std::vector<double> result;
    for (const auto& value : reports) {
        const auto& measurement = value.as_object().at(metric.section).as_object().at(metric.field);
        if (measurement.is_null()) {
            continue;
        }
        const auto& number =
            metric.statistic.empty() ? measurement : measurement.as_object().at(metric.statistic);
        result.push_back(number.to_number<double>());
    }
    return result;
}
json::array checked_runs(const std::filesystem::path& directory, const json::object& suite) {
    if (suite.at("schema") != "engine.benchmark.suite.v1" || suite.at("status") != "complete") {
        throw std::invalid_argument("Benchmark suite is incomplete");
    }
    json::array reports;
    std::set<std::string> directories;
    for (const auto& value : suite.at("runs").as_array()) {
        const auto& entry = value.as_object();
        const auto name = text(entry.at("directory"));
        if (!name.starts_with("run_") || std::filesystem::path(name).filename() != name ||
            !directories.insert(name).second) {
            throw std::invalid_argument("Invalid worker result path");
        }
        if (platform::process::file_sha256(directory / name / "summary.json") !=
            text(entry.at("sha256"))) {
            throw std::invalid_argument("Worker summary integrity mismatch");
        }
        reports.push_back(checked_report(directory / name));
    }
    if (reports.empty() || reports.size() != suite.at("expected_runs").to_number<std::size_t>()) {
        throw std::invalid_argument("Missing benchmark repetitions");
    }
    return reports;
}
} // namespace
json::array read_runs(const std::filesystem::path& directory) {
    const auto suite = directory / "suite.json";
    auto reports = std::filesystem::exists(suite)
                       ? checked_runs(directory, json::parse(read_text(suite)).as_object())
                       : json::array{checked_report(directory)};
    for (const auto& report : reports) {
        compatible(reports.front().as_object(), report.as_object());
        if (reports.front().as_object().at("build") != report.as_object().at("build")) {
            throw std::invalid_argument("A suite must use one executable and shader set");
        }
    }
    return reports;
}
json::object aggregate(const json::array& reports) {
    json::object result;
    for (const auto& metric : metrics()) {
        result[metric.name] = distribution(values(reports, metric));
    }
    return result;
}
int compare(const Options& options) {
    try {
        const auto baseline = read_runs(options.baseline);
        const auto candidate = read_runs(options.candidate);
        compatible(baseline.front().as_object(), candidate.front().as_object());
        json::object effects;
        for (const auto& metric : metrics()) {
            effects[metric.name] = effect(values(baseline, metric), values(candidate, metric));
        }
        json::object result{{"schema", "engine.benchmark.comparison.v1"},
                            {"compatible", true},
                            {"workload_sha256", baseline.front().as_object().at("workload_sha256")},
                            {"baseline_runs", baseline.size()},
                            {"candidate_runs", candidate.size()},
                            {"metrics", std::move(effects)},
                            {"ratio_definition", "candidate / baseline; lower means less time or "
                                                 "memory; higher means more throughput"}};
        if (!options.output.empty()) {
            if (std::filesystem::exists(options.output)) {
                throw std::invalid_argument("Comparison output already exists");
            }
            write_json(options.output, result);
        }
        std::cout << json::serialize(result) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cannot compare: " << error.what() << '\n';
        return 4;
    }
}
} // namespace engine::app::benchmark
