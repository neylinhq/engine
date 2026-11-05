#include "command.hpp"
#include <engine/platform/process/process.hpp>
#include <exception>
#include <iostream>
namespace {
int run(const std::vector<std::string>& arguments) {
    try {
        return engine::app::benchmark::dispatch(engine::app::benchmark::parse_options(arguments));
    } catch (const std::exception& error) {
        std::cerr << "Benchmark: " << error.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int count, wchar_t** values) {
    std::vector<std::string> arguments;
    for (int index = 0; index < count; ++index) {
        arguments.push_back(engine::platform::process::path_text(values[index]));
    }
    return run(arguments);
}
#else
int main(int count, char** values) {
    return run(std::vector<std::string>(values, values + count));
}
#endif
