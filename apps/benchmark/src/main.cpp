#include "command.hpp"
#include <engine/platform/process/process.hpp>
#include <exception>
#include <iostream>
namespace engine::app::benchmark {
namespace {
int run(const std::vector<std::string>& arguments) {
    try {
        return dispatch(parse_options(arguments));
    } catch (const std::exception& error) {
        std::cerr << "Benchmark: " << error.what() << '\n';
        return 2;
    }
}
} // namespace
} // namespace engine::app::benchmark
#ifdef _WIN32
int wmain(int count, wchar_t** values) {
    std::vector<std::string> arguments;
    for (int index = 0; index < count; ++index) {
        arguments.push_back(engine::platform::process::path_text(values[index]));
    }
    return engine::app::benchmark::run(arguments);
}
#else
int main(int count, char** values) {
    return engine::app::benchmark::run(std::vector<std::string>(values, values + count));
}
#endif
