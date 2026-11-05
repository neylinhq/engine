#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <engine/platform/process/process.hpp>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace engine::app::benchmark {
void keys(const json::object& object, std::initializer_list<std::string_view> allowed) {
    for (const auto& member : object) {
        if (std::find(allowed.begin(), allowed.end(), member.key()) == allowed.end()) {
            throw std::invalid_argument("Unknown field: " + std::string(member.key()));
        }
    }
}
unsigned integer(const json::value& value, unsigned minimum, unsigned maximum) {
    const auto result = value.to_number<double>();
    if (!std::isfinite(result) || std::trunc(result) != result || result < minimum ||
        result > maximum) {
        throw std::invalid_argument("Integer outside supported range");
    }
    return static_cast<unsigned>(result);
}
double real(const json::value& value, double minimum, double maximum) {
    const auto result = value.to_number<double>();
    if (!std::isfinite(result) || result < minimum || result > maximum) {
        throw std::invalid_argument("Number outside supported finite range");
    }
    return result;
}
std::string text(const json::value& value) {
    const auto& result = value.as_string();
    if (result.empty() || result.find('\0') != json::string::npos) {
        throw std::invalid_argument("Expected nonempty text without NUL");
    }
    return std::string(result);
}
foundation::geometry::Vec3 vector(const json::value& value) {
    const auto& array = value.as_array();
    if (array.size() != 3) {
        throw std::invalid_argument("Vector requires three values");
    }
    return {real(array[0], -1e9, 1e9), real(array[1], -1e9, 1e9), real(array[2], -1e9, 1e9)};
}
world::model::IVec3 voxel(const json::value& value) {
    const auto v = vector(value);
    if (std::trunc(v.x) != v.x || std::trunc(v.y) != v.y || std::trunc(v.z) != v.z) {
        throw std::invalid_argument("Voxel coordinates must be integers");
    }
    return {static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z)};
}
std::string read_text(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 16 * 1024 * 1024) {
        throw std::invalid_argument("JSON input exceeds 16 MiB");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::invalid_argument("Cannot open JSON: " + platform::process::path_text(path));
    }
    std::string result{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
        throw std::runtime_error("Cannot read JSON");
    }
    return result;
}
std::filesystem::path relative_file(const json::value& value,
                                    const std::filesystem::path& directory) {
    return std::filesystem::absolute(directory / platform::process::text_path(text(value)))
        .lexically_normal();
}
json::value canonical(const json::value& value) {
    if (value.is_object()) {
        std::vector<std::string> names;
        for (const auto& member : value.as_object()) {
            names.emplace_back(member.key());
        }
        std::ranges::sort(names);
        json::object result;
        for (const auto& name : names) {
            result[name] = canonical(value.as_object().at(name));
        }
        return result;
    }
    if (value.is_array()) {
        json::array result;
        for (const auto& member : value.as_array()) {
            result.push_back(canonical(member));
        }
        return result;
    }
    return value;
}
std::string fingerprint(const json::value& value) {
    const auto bytes = json::serialize(canonical(value));
    return platform::process::sha256(std::as_bytes(std::span(bytes)));
}
void write_json(const std::filesystem::path& path, const json::value& value) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << json::serialize(canonical(value)) << '\n';
    file.close();
    if (!file) {
        throw std::runtime_error("Cannot write result: " + platform::process::path_text(path));
    }
}
} // namespace engine::app::benchmark
