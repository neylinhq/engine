#include "application.hpp"
#include "io/path.hpp"
#include <exception>
#include <iostream>
#include <vector>
namespace {
int run_arguments(int count, char** arguments) {
    try {
        auto options = engine::app::sandbox::parse_options(count, arguments);
        if (options.help) {
            engine::app::sandbox::print_usage();
            return 0;
        }
        return engine::app::sandbox::run(std::move(options));
    } catch (const std::exception& error) {
        std::cerr << "Sandbox: " << error.what() << '\n';
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int count, wchar_t** arguments) {
    try {
        std::vector<std::string> utf8;
        utf8.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            utf8.push_back(
                engine::app::sandbox::path_to_utf8(std::filesystem::path(arguments[index])));
        }
        std::vector<char*> values;
        values.reserve(utf8.size());
        for (auto& value : utf8) {
            values.push_back(value.data());
        }
        return run_arguments(count, values.data());
    } catch (const std::exception& error) {
        std::cerr << "Sandbox: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int count, char** arguments) {
    return run_arguments(count, arguments);
}
#endif
