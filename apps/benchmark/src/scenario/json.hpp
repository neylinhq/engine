#ifndef ENGINE_APP_BENCHMARK_SRC_SCENARIO_JSON_HPP
#define ENGINE_APP_BENCHMARK_SRC_SCENARIO_JSON_HPP
#include "scenario.hpp"
#include <initializer_list>
#include <string_view>
namespace engine::app::benchmark {
void keys(const json::object& object, std::initializer_list<std::string_view> allowed);
[[nodiscard]] unsigned integer(const json::value& value, unsigned minimum, unsigned maximum);
[[nodiscard]] double real(const json::value& value, double minimum, double maximum);
[[nodiscard]] std::string text(const json::value& value);
[[nodiscard]] world::model::Vec3i voxel(const json::value& value);
[[nodiscard]] foundation::geometry::Vec3 vector(const json::value& value);
[[nodiscard]] std::string read_text(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path relative_file(const json::value& value,
                                                  const std::filesystem::path& directory);
[[nodiscard]] json::value canonical(const json::value& value);
[[nodiscard]] std::string fingerprint(const json::value& value);
void write_json(const std::filesystem::path& path, const json::value& value);
} // namespace engine::app::benchmark
#endif
