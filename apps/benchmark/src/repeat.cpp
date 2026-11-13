#include "command.hpp"
#include "report/report.hpp"
#include "scenario/json.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
namespace engine::app::benchmark {
namespace {
platform::process::Command worker(const Options& options, const std::filesystem::path& output,
                                  unsigned index) {
    using platform::process::path_text;
    platform::process::Command command{
        platform::process::executable(),
        {"measure", "--scenario", path_text(std::filesystem::absolute(options.scenario)),
         "--shaders", path_text(std::filesystem::absolute(options.shaders)), "--output",
         path_text(std::filesystem::absolute(output)), "--run-index", std::to_string(index)}};
    const auto add_path = [&](const char* option, const std::filesystem::path& path) {
        if (!path.empty()) {
            command.arguments.insert(command.arguments.end(),
                                     {option, path_text(std::filesystem::absolute(path))});
        }
    };
    add_path("--scene", options.scene);
    add_path("--materials", options.materials);
    add_path("--settings", options.settings);
    if (!options.label.empty()) {
        command.arguments.insert(command.arguments.end(), {"--label", options.label});
    }
    if (options.hidden) {
        command.arguments.push_back("--hidden");
    }
    if (options.validation) {
        command.arguments.push_back("--validation");
    }
    if (options.capture) {
        command.arguments.push_back("--capture");
    }
    return command;
}
std::string run_name(unsigned index) {
    std::ostringstream name;
    name << "run_" << std::setfill('0') << std::setw(3) << index;
    return name.str();
}
void record_run(json::array& reports, json::object& suite, const std::filesystem::path& path) {
    auto report = read_runs(path).front().as_object();
    if (!reports.empty()) {
        const auto& first = reports.front().as_object();
        if (first.at("workload_sha256") != report.at("workload_sha256") ||
            first.at("environment") != report.at("environment") ||
            first.at("build") != report.at("build")) {
            throw std::runtime_error("Inputs, environment or binaries changed between repetitions");
        }
    }
    reports.push_back(std::move(report));
    suite.at("runs").as_array().push_back(
        json::object{{"directory", path.filename().string()},
                     {"sha256", platform::process::file_sha256(path / "summary.json")}});
}
void execute_worker(const Options& options, const std::filesystem::path& path, unsigned index) {
    const auto result = platform::process::run(worker(options, path, index));
    if (result != 0) {
        if (std::filesystem::exists(path / "failure.json")) {
            const auto failure = json::parse(read_text(path / "failure.json")).as_object();
            throw std::runtime_error("Worker failed: " + text(failure.at("message")));
        }
        throw std::runtime_error("Worker exit code " + std::to_string(result) + "; see " +
                                 path.string());
    }
}
} // namespace
int repeat(const Options& options) {
    if (!options.output.parent_path().empty()) {
        std::filesystem::create_directories(options.output.parent_path());
    }
    if (!std::filesystem::create_directory(options.output)) {
        throw std::invalid_argument("Output directory must not already exist");
    }
    json::object suite{{"schema", "engine.benchmark.suite.v1"},
                       {"status", "running"},
                       {"expected_runs", options.repetitions},
                       {"runs", json::array{}}};
    json::array reports;
    try {
        write_json(options.output / "suite.json", suite);
        for (unsigned index = 0; index < options.repetitions; ++index) {
            const auto path = options.output / run_name(index);
            std::cerr << "Benchmark repetition " << index + 1 << '/' << options.repetitions << '\n';
            execute_worker(options, path, index);
            record_run(reports, suite, path);
            write_json(options.output / "suite.json", suite);
        }
        suite["status"] = "complete";
        suite["run_level_statistics"] = aggregate(reports);
        write_json(options.output / "suite.json", suite);
        return 0;
    } catch (const std::exception& error) {
        suite["status"] = "failed";
        suite["message"] = error.what();
        write_json(options.output / "suite.json", suite);
        std::cerr << error.what() << '\n';
        return 3;
    }
}
} // namespace engine::app::benchmark
