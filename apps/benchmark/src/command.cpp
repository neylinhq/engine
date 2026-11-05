#include "command.hpp"
#include "scenario/json.hpp"
#include <charconv>
#include <engine/platform/process/process.hpp>
#include <iostream>
#include <set>
#include <stdexcept>
namespace engine::app::benchmark {
namespace {
unsigned count(std::string_view value, unsigned maximum) {
    unsigned result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result > maximum) {
        throw std::invalid_argument("Invalid count: " + std::string(value));
    }
    return result;
}
void option(Options& options, const std::string& name, const std::string& value) {
    using platform::process::text_path;
    if (name == "--scenario") {
        options.scenario = text_path(value);
    } else if (name == "--scene") {
        options.scene = text_path(value);
    } else if (name == "--shaders") {
        options.shaders = text_path(value);
    } else if (name == "--output") {
        options.output = text_path(value);
    } else if (name == "--baseline") {
        options.baseline = text_path(value);
    } else if (name == "--candidate") {
        options.candidate = text_path(value);
    } else if (name == "--label") {
        options.label = value;
    } else if (name == "--repetitions") {
        options.repetitions = count(value, 100);
    } else if (name == "--run-index") {
        options.run_index = count(value, 99);
    } else {
        throw std::invalid_argument("Unknown option: " + name);
    }
}
std::set<std::string> allowed_options(std::string_view command) {
    if (command == "compare") {
        return {"--baseline", "--candidate", "--output"};
    }
    if (command == "validate") {
        return {"--scenario", "--scene", "--shaders"};
    }
    std::set<std::string> allowed{"--scenario", "--scene",  "--shaders",    "--output",
                                  "--label",    "--hidden", "--validation", "--capture"};
    if (command == "run") {
        allowed.insert("--repetitions");
    } else if (command == "measure") {
        allowed.insert("--run-index");
    } else {
        throw std::invalid_argument("Command must be run, measure, validate or compare");
    }
    return allowed;
}
void check_options(const Options& options, const std::set<std::string>& used) {
    const auto allowed = allowed_options(options.command);
    if (options.command == "compare") {
        if (options.baseline.empty() || options.candidate.empty()) {
            throw std::invalid_argument("Compare requires --baseline and --candidate");
        }
    } else if (options.scenario.empty() ||
               (options.command != "validate" && options.output.empty())) {
        throw std::invalid_argument("A scenario and a new output directory are required");
    }
    for (const auto& name : used) {
        if (!allowed.contains(name)) {
            throw std::invalid_argument("Option does not apply to this command: " + name);
        }
    }
    if (options.repetitions == 0) {
        throw std::invalid_argument("Repetitions must be positive");
    }
}
} // namespace
Options parse_options(const std::vector<std::string>& arguments) {
    Options options;
    if (arguments.size() < 2 || arguments[1] == "--help" || arguments[1] == "help") {
        options.command = "help";
        return options;
    }
    options.command = arguments[1];
    options.shaders = platform::process::executable().parent_path() / "shaders";
    std::set<std::string> used;
    for (std::size_t index = 2; index < arguments.size(); ++index) {
        const auto& name = arguments[index];
        if (!used.insert(name).second) {
            throw std::invalid_argument("Duplicate option: " + name);
        }
        if (name == "--hidden") {
            options.hidden = true;
        } else if (name == "--validation") {
            options.validation = true;
        } else if (name == "--capture") {
            options.capture = true;
        } else {
            if (++index == arguments.size()) {
                throw std::invalid_argument("Missing option value: " + name);
            }
            option(options, name, arguments[index]);
        }
    }
    check_options(options, used);
    return options;
}
void print_usage() {
    std::cout << "Engine Benchmark\n"
                 "  benchmark validate --scenario case.json [--scene world.cvox]\n"
                 "  benchmark run --scenario case.json --output new-directory [--repetitions 5]\n"
                 "  benchmark measure --scenario case.json --output new-directory\n"
                 "  benchmark compare --baseline old-directory --candidate new-directory [--output "
                 "comparison.json]\n"
                 "Run/measure options: --scene FILE --shaders DIR --label TEXT --hidden "
                 "--validation --capture\n"
                 "Visible Vulkan surface, immediate presentation and no UI by default.\n"
                 "Validation and hidden surfaces are recorded explicitly. Existing results are "
                 "never overwritten.\n";
}
int dispatch(const Options& options) {
    if (options.command == "help") {
        print_usage();
        return 0;
    }
    if (options.command == "compare") {
        return compare(options);
    }
    const auto scenario = load_scenario(options);
    if (options.command == "validate") {
        std::cout << json::serialize(json::object{{"schema", "engine.benchmark.validation.v1"},
                                                  {"name", scenario.name},
                                                  {"valid", true}})
                  << '\n';
        return 0;
    }
    return options.command == "run" ? repeat(options) : measure(options);
}
} // namespace engine::app::benchmark
