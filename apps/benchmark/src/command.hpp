#ifndef ENGINE_APP_BENCHMARK_COMMAND_HPP
#define ENGINE_APP_BENCHMARK_COMMAND_HPP
#include <filesystem>
#include <string>
#include <vector>
namespace engine::app::benchmark {
struct Options {
    std::string command;
    std::filesystem::path scenario;
    std::filesystem::path scene;
    std::filesystem::path shaders;
    std::filesystem::path output;
    std::filesystem::path baseline;
    std::filesystem::path candidate;
    std::string label;
    unsigned repetitions = 5;
    unsigned run_index = 0;
    bool hidden = false;
    bool validation = false;
    bool capture = false;
};
[[nodiscard]] Options parse_options(const std::vector<std::string>& arguments);
void print_usage();
[[nodiscard]] int dispatch(const Options& options);
[[nodiscard]] int measure(const Options& options);
[[nodiscard]] int repeat(const Options& options);
[[nodiscard]] int compare(const Options& options);
} // namespace engine::app::benchmark
#endif
