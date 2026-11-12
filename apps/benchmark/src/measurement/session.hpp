#ifndef ENGINE_APP_BENCHMARK_SRC_MEASUREMENT_SESSION_HPP
#define ENGINE_APP_BENCHMARK_SRC_MEASUREMENT_SESSION_HPP
#include "../scenario/scenario.hpp"
#include <chrono>
#include <engine/platform/process/process.hpp>
#include <engine/platform/window/window.hpp>
#include <engine/world/model/world.hpp>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
namespace engine::app::benchmark {
using Clock = std::chrono::steady_clock;
template <typename T> T take(std::expected<T, std::string> value) {
    if (!value) {
        throw std::runtime_error(value.error());
    }
    return std::move(*value);
}
inline void take(std::expected<void, std::string> value) {
    if (!value) {
        throw std::runtime_error(value.error());
    }
}
inline double elapsed(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
struct FrameSample {
    unsigned frame = 0;
    std::uint64_t submission = 0;
    motion::camera::Pose camera;
    unsigned events = 0;
    std::uint64_t revision = 0;
    std::uint64_t renderer_bytes = 0;
    double wall_ms = 0;
    double edit_ms = 0;
    double wait_ms = 0;
    double record_ms = 0;
    double submit_ms = 0;
    std::optional<double> gpu_ms;
    std::optional<double> gpu_prepare_ms;
    std::optional<double> gpu_render_ms;
    std::vector<rendering::vulkan::PassTiming> passes;
};
struct EventSample {
    std::size_t index = 0;
    unsigned frame = 0;
    double wall_ms = 0;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
    std::size_t entities_after = 0;
};
struct Measurements {
    std::vector<FrameSample> frames;
    std::vector<EventSample> events;
    json::array memory;
    json::object assets;
    json::object contract;
    json::object environment;
    json::object build;
    double scene_load_ms = 0;
    double generation_ms = 0;
    double setup_ms = 0;
    double warmup_ms = 0;
    double measurement_ms = 0;
    double final_drain_ms = 0;
    double observation_ms = 0;
    std::uint64_t first_submission = 0;
};
struct Session {
    Options options;
    Scenario scenario;
    world::model::World world{std::thread::hardware_concurrency()};
    std::unique_ptr<platform::window::Window> window;
    std::unique_ptr<rendering::vulkan::Renderer> renderer;
    std::optional<motion::camera::Camera> camera;
    std::map<std::filesystem::path, std::size_t> models;
    std::map<std::filesystem::path, std::size_t> entity_types;
    std::map<std::string, std::size_t> entities;
    std::size_t next_event = 0;
    Measurements results;
};
void initialize(Session& session);
void execute(Session& session);
void apply_events(Session& session, FrameSample& sample);
void collect_gpu(Measurements& result, const rendering::vulkan::Statistics& statistics);
void observe_memory(Session& session, std::string_view phase, std::optional<unsigned> frame = {});
void identify(Session& session);
[[nodiscard]] json::object model_identity(const world::model::Model& model);
void finish_report(Session& session);
void verify_probes(const Session& session);
} // namespace engine::app::benchmark
#endif
