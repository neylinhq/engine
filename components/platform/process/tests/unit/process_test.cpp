#include <chrono>
#include <engine/platform/process/process.hpp>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
namespace {
namespace process = engine::platform::process;
void require(bool value) {
    if (!value) {
        throw std::runtime_error("Process contract failed");
    }
}
std::optional<int> helper(const std::vector<std::string>& arguments) {
    if (arguments.size() == 3 && arguments[1] == "--wait") {
        std::ofstream file(process::text_path(arguments[2]));
        file << process::identity().process_id;
        file.close();
        require(bool(file));
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }
    if (arguments.size() == 3 && arguments[1] == "--supervise") {
        return process::run({process::executable(), {"--wait", arguments[2]}});
    }
    return std::nullopt;
}
int check(const std::vector<std::string>& arguments) {
    if (const auto status = helper(arguments)) {
        return *status;
    }
    const std::vector<std::string> payload{"a b", "quoted\"slash\\", "Юникод путь\\"};
    if (arguments.size() > 1) {
        require(std::vector<std::string>(arguments.begin() + 1, arguments.end()) == payload);
        return 23;
    }
    const std::string value = "abc";
    const std::string expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    require(process::sha256(std::as_bytes(std::span(value))) == expected);
    const auto path = std::filesystem::temp_directory_path() /
                      process::text_path("engine_проверка_" +
                                         std::to_string(process::identity().process_id) + ".txt");
    require(!std::filesystem::exists(path));
    {
        std::ofstream stream(path, std::ios::binary);
        stream << value;
    }
    const auto actual = process::file_sha256(path);
    std::filesystem::remove(path);
    require(actual == expected);
    require(process::text_path(process::path_text(path)) == path);
    const auto memory = process::memory();
    require(memory.resident_bytes > 0 && memory.peak_resident_bytes > 0);
    require(process::identity().process_id > 0);
    require(process::run({process::executable(), payload}) == 23);
    return 0;
}
int run(const std::vector<std::string>& arguments) {
    try {
        return check(arguments);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int count, wchar_t** values) {
    std::vector<std::string> arguments;
    for (int index = 0; index < count; ++index) {
        arguments.push_back(process::path_text(values[index]));
    }
    return run(arguments);
}
#else
int main(int count, char** values) {
    return run(std::vector<std::string>(values, values + count));
}
#endif
