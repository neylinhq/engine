#ifndef ENGINE_APP_BENCHMARK_SRC_SCENARIO_SCENARIO_HPP
#define ENGINE_APP_BENCHMARK_SRC_SCENARIO_SCENARIO_HPP
#include "../command.hpp"
#include <boost/json.hpp>
#include <engine/foundation/geometry/vector.hpp>
#include <engine/motion/camera/camera.hpp>
#include <engine/rendering/settings/settings.hpp>
#include <engine/rendering/vulkan/renderer.hpp>
#include <engine/world/editing/editing.hpp>
#include <engine/world/model/types.hpp>
#include <vector>
namespace engine::app::benchmark {
namespace json = boost::json;
struct Keyframe {
    unsigned frame = 0;
    motion::camera::Pose pose;
};
enum class Operation { brush, fill, stamp, spawn, move_entity, clear_entities };
struct Event {
    unsigned frame = 0;
    Operation operation = Operation::brush;
    world::editing::Brush brush;
    world::model::Vec3i voxel;
    foundation::geometry::Vec3 position;
    std::filesystem::path model;
    std::string entity;
    std::uint16_t material = 1;
};
struct Probe {
    world::model::Vec3i voxel;
    std::uint16_t material = 0;
};
struct Scenario {
    std::string name;
    std::filesystem::path scene;
    std::filesystem::path materials;
    std::filesystem::path settings_file;
    world::model::Vec3i empty_size{256, 256, 256};
    double voxel_size = 1;
    unsigned frames = 600;
    unsigned warmup_frames = 4000;
    unsigned memory_interval = 60;
    unsigned bounds_budget = 32768;
    double step_seconds = 1.0 / 60.0;
    unsigned width = 1920;
    unsigned height = 1080;
    bool borderless = false;
    rendering::settings::Settings settings;
    rendering::vulkan::Configuration renderer;
    std::vector<Keyframe> camera;
    std::vector<Event> events;
    std::vector<Probe> probes;
    json::object document;
};
[[nodiscard]] Scenario load_scenario(const Options& options);
[[nodiscard]] Scenario parse_scenario(const json::object& root,
                                      const std::filesystem::path& directory,
                                      const std::filesystem::path& settings_override = {});
[[nodiscard]] std::vector<Event>
parse_events(const json::array& values, const std::filesystem::path& directory, unsigned frames);
[[nodiscard]] std::vector<Keyframe> parse_camera(const json::array& values, unsigned frames);
[[nodiscard]] motion::camera::Pose camera_at(const std::vector<Keyframe>& camera, unsigned frame);
} // namespace engine::app::benchmark
#endif
