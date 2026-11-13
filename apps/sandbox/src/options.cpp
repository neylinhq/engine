#include "options.hpp"
#include "io/executable.hpp"
#include "io/path.hpp"
#include <charconv>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
namespace engine::app::sandbox {
namespace {
double real(std::string_view text) {
    double result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(result)) {
        throw std::invalid_argument("Invalid finite number: " + std::string(text));
    }
    return result;
}
std::array<double, 5> camera_pose(std::string_view text) {
    std::array<double, 5> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto end = text.find(',');
        if ((end == std::string_view::npos) != (index + 1 == result.size())) {
            throw std::invalid_argument("Camera requires x,y,z,yaw,pitch in meters/radians");
        }
        result[index] = real(text.substr(0, end));
        if (end != std::string_view::npos) {
            text.remove_prefix(end + 1);
        }
    }
    return result;
}
unsigned number(std::string_view text) {
    unsigned result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("Invalid nonnegative integer: " + std::string(text));
    }
    return result;
}
rendering::settings::RenderVersion mode(std::string_view value) {
    using rendering::settings::RenderVersion;
    if (value == "albedo") {
        return RenderVersion::albedo;
    }
    if (value == "base") {
        return RenderVersion::base;
    }
    if (value == "path" || value == "path_tracer") {
        return RenderVersion::path_tracer;
    }
    throw std::invalid_argument("Mode must be albedo, base or path");
}
rendering::vulkan::ChunkLayout chunk_layout(std::string_view value) {
    using rendering::vulkan::ChunkLayout;
    if (value == "automatic") {
        return ChunkLayout::automatic;
    }
    if (value == "packed") {
        return ChunkLayout::packed;
    }
    if (value == "split") {
        return ChunkLayout::split;
    }
    throw std::invalid_argument("Chunk layout must be automatic, packed or split");
}
bool choice(std::string_view value, std::string_view enabled, std::string_view disabled) {
    if (value == enabled) {
        return true;
    }
    if (value == disabled) {
        return false;
    }
    throw std::invalid_argument("Expected " + std::string(enabled) + " or " +
                                std::string(disabled));
}
void value_option(Options& result, std::string_view option, std::string_view value) {
    if (option == "--scene") {
        result.scene = path_from_utf8(value);
    } else if (option == "--shaders") {
        result.shaders = path_from_utf8(value);
    } else if (option == "--settings") {
        result.settings = path_from_utf8(value);
    } else if (option == "--screenshot") {
        result.screenshot = path_from_utf8(value);
    } else if (option == "--frames") {
        result.frames = number(value);
    } else if (option == "--width") {
        result.width = static_cast<int>(number(value));
    } else if (option == "--height") {
        result.height = static_cast<int>(number(value));
    } else if (option == "--mode") {
        result.mode = mode(value);
        result.mode_set = true;
    } else if (option == "--chunk-layout") {
        result.chunk_layout = chunk_layout(value);
    } else if (option == "--frame-wait") {
        result.defer_frame_wait = choice(value, "deferred", "early");
    } else if (option == "--output-path") {
        result.offscreen_output = choice(value, "offscreen", "automatic");
    } else if (option == "--voxel-size") {
        result.voxel_meters = real(value);
    } else if (option == "--camera") {
        result.camera_meters = camera_pose(value);
    } else {
        throw std::invalid_argument("Unknown option: " + std::string(option));
    }
}
bool flag_option(Options& result, std::string_view option) {
    if (option == "--hidden") {
        result.hidden = true;
    } else if (option == "--walk") {
        result.walk = true;
    } else if (option == "--no-ui") {
        result.no_ui = true;
    } else if (option == "--validation") {
        result.validation = true;
    } else if (option == "--help" || option == "-h") {
        result.help = true;
    } else {
        return false;
    }
    return true;
}
} // namespace
Options parse_options(int count, char** arguments) {
    Options result;
    const auto executable = executable_directory(arguments[0]);
    result.shaders = executable / "shaders";
    for (int index = 1; index < count; ++index) {
        const auto option = std::string_view(arguments[index]);
        if (flag_option(result, option)) {
            continue;
        }
        if (++index >= count) {
            throw std::invalid_argument("Missing value for " + std::string(option));
        }
        value_option(result, option, arguments[index]);
    }
    if (result.width < 1 || result.height < 1 || result.width > 16384 || result.height > 16384) {
        throw std::invalid_argument("Window dimensions must be in 1..16384");
    }
    if (result.voxel_meters < 1.0 / 1024 || result.voxel_meters > 1) {
        throw std::invalid_argument("Voxel size must be in [1/1024, 1] meters");
    }
    if (!result.screenshot.empty() && result.frames == 0) {
        result.frames = 1;
    }
    return result;
}
void print_usage() {
    std::cout << "Sandbox / Engine C++23 / Vulkan / Slang\n"
                 "  --scene FILE.cvox|.vox|.vl32|.obj|.stl  --shaders DIRECTORY\n"
                 "  Without --scene, start an empty world and open a file from the menu.\n"
                 "  --mode albedo|base|path  --width N --height N --frames N\n"
                 "  --screenshot FILE.png  --hidden  --no-ui  --validation\n"
                 "  --settings FILE.json\n"
                 "  --chunk-layout automatic|packed|split (default: automatic)\n"
                 "  --frame-wait deferred|early (default: deferred)\n"
                 "  --output-path automatic|offscreen (default: automatic)\n"
                 "  --voxel-size METERS  --camera X,Y,Z,YAW,PITCH (meters/radians)  --walk\n"
                 "WASD fly, mouse look, Shift boost, Space ascend, Ctrl descend.\n"
                 "F toggles walk/flight. In walk mode Shift runs and Space jumps.\n"
                 "Tab or F1 UI/capture, Esc release, F2 path, F11 borderless.\n";
}
} // namespace engine::app::sandbox
