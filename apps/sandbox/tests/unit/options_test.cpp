#include "../../src/io/executable.hpp"
#include "../../src/io/path.hpp"
#include "../../src/options.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace engine::app::sandbox {
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void unicode_arguments() {
    using namespace engine::app::sandbox;
    const std::filesystem::path scene =
        u8"\u043c\u043e\u0434\u0435\u043b\u0438/\u043e\u0430\u0437\u0438\u0441.cvox";
    const std::filesystem::path capture = u8"\u0441\u043d\u0438\u043c\u043e\u043a.png";
    std::array<std::string, 5> values{"engine", "--scene", path_to_utf8(scene), "--screenshot",
                                      path_to_utf8(capture)};
    std::array<char*, 5> arguments{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        arguments[index] = values[index].data();
    }
    const auto options = parse_options(static_cast<int>(arguments.size()), arguments.data());
    require(options.scene == scene, "CLI scene lost Unicode path");
    require(options.screenshot == capture && options.frames == 1,
            "CLI screenshot lost Unicode path or implicit frame count");
    require(path_from_utf8(path_to_utf8(scene)) == scene,
            "UTF-8 path conversion lost native spelling");
}
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
void unicode_file() {
    using namespace engine::app::sandbox;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto name = std::u8string(u8"\u0442\u0435\u0441\u0442_") +
                      path_from_utf8(std::to_string(stamp)).u8string();
    TemporaryFile file{std::filesystem::temp_directory_path() / name};
    {
        std::ofstream stream(path_from_utf8(path_to_utf8(file.path)), std::ios::binary);
        stream << "native Unicode file";
        require(static_cast<bool>(stream), "Could not write native Unicode path");
    }
    std::ifstream stream(file.path, std::ios::binary);
    std::string value;
    std::getline(stream, value);
    require(value == "native Unicode file", "UTF-8 boundary wrote a different native filename");
}
void path_launch() {
    using namespace engine::app::sandbox;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto name = "engine_path_" + std::to_string(stamp);
#ifdef _WIN32
    name += ".exe";
#endif
    TemporaryFile file{std::filesystem::temp_directory_path() / name};
    {
        std::ofstream stream(file.path, std::ios::binary);
        stream << "executable fixture";
    }
#ifndef _WIN32
    std::filesystem::permissions(file.path, std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::add);
#endif
    const auto located =
        find_executable(file.path.filename(), path_to_utf8(file.path.parent_path()));
    require(located == std::filesystem::canonical(file.path),
            "PATH launch resolved resources under the working directory");
    require(find_executable(file.path, {}) == std::filesystem::canonical(file.path),
            "Explicit executable path was replaced by PATH search");
}
template <std::size_t count>
engine::app::sandbox::Options parse(std::array<std::string, count> values) {
    std::array<char*, count> arguments{};
    for (std::size_t index = 0; index < count; ++index) {
        arguments[index] = values[index].data();
    }
    return engine::app::sandbox::parse_options(static_cast<int>(count), arguments.data());
}
void scene_units() {
    require(parse(std::array<std::string, 1>{"engine"}).scene.empty(),
            "Startup without --scene must not require a bundled map");
    require(!parse(std::array<std::string, 1>{"engine"}).walk &&
                parse(std::array<std::string, 2>{"engine", "--walk"}).walk,
            "Walking must be selected explicitly at startup");
    const auto options = parse(std::array<std::string, 5>{"engine", "--voxel-size", "0.03125",
                                                          "--camera", "256,104,112,0,-0.08"});
    require(options.voxel_metres == 1.0 / 32 && options.camera_metres &&
                *options.camera_metres == std::array<double, 5>{256, 104, 112, 0, -0.08},
            "Fine-voxel scene lost its physical camera or voxel scale");
    for (const auto* bad : {"0", "-0.01", "nan", "inf", "1.01"}) {
        bool rejected = false;
        try {
            static_cast<void>(parse(std::array<std::string, 3>{"engine", "--voxel-size", bad}));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "Invalid physical voxel size was accepted");
    }
    for (const auto* bad : {"1,2,3,4", "1,2,3,4,5,6", "1,2,nan,0,0", "1,2,3,0,"}) {
        bool rejected = false;
        try {
            static_cast<void>(parse(std::array<std::string, 3>{"engine", "--camera", bad}));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "Malformed physical camera was accepted");
    }
}
int run_options_tests() {
    try {
        unicode_arguments();
        unicode_file();
        path_launch();
        scene_units();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
} // namespace
} // namespace engine::app::sandbox

int main() {
    return engine::app::sandbox::run_options_tests();
}
