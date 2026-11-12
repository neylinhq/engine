#include "application.hpp"
#include "io/path.hpp"
#include <exception>
#include <iostream>
#include <vector>
namespace engine::app::sandbox {
namespace {
int run_arguments(int count, char** arguments) {
    try {
        auto options = parse_options(count, arguments);
        if (options.help) {
            print_usage();
            return 0;
        }
        return run(std::move(options));
    } catch (const std::exception& error) {
        std::cerr << "Sandbox: " << error.what() << '\n';
        return 1;
    }
}
} // namespace
} // namespace engine::app::sandbox
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
        return engine::app::sandbox::run_arguments(count, values.data());
    } catch (const std::exception& error) {
        std::cerr << "Sandbox: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int count, char** arguments) {
    return engine::app::sandbox::run_arguments(count, arguments);
}
#endif
