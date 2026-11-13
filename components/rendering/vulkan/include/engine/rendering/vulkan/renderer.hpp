#ifndef ENGINE_RENDERING_VULKAN_RENDERER_HPP
#define ENGINE_RENDERING_VULKAN_RENDERER_HPP

#include <array>
#include <cstdint>
#include <engine/motion/camera/camera.hpp>
#include <engine/platform/window/window.hpp>
#include <engine/rendering/settings/settings.hpp>
#include <engine/world/model/world.hpp>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace engine::rendering::vulkan {

enum class ChunkLayout { automatic, packed, split };
enum class PresentationMode { automatic, immediate, mailbox, fifo };

struct Configuration {
    std::filesystem::path shaders = "shaders";
    bool validation = false;
    bool vertical_sync = false;
    // Adds a timestamp between resource/world preparation and rendering.
    bool profile_world_updates = false;
    // Adds a timestamp after each render pass; disabled during ordinary rendering.
    bool profile_render_passes = false;
    ChunkLayout chunk_layout = ChunkLayout::automatic;
    // Allows UI/camera/world CPU preparation while the preceding GPU frame completes.
    bool defer_frame_wait = true;
    // Retains the offscreen presentation path for capability fallback and comparisons.
    bool offscreen_output = false;
    PresentationMode presentation_mode = PresentationMode::automatic;
    std::string application_name = "Engine";
};

struct FrameInput {
    world::model::WorldView world;
    const world::model::EntityGpuData* entities = nullptr;
    const motion::camera::Camera* camera = nullptr;
    const settings::Settings* settings = nullptr;
    double delta_seconds = 0;
    // Inactive windows keep rendering but suspend the world update and sampling clock.
    bool active = true;
    bool paused = false;
    bool reset_accumulation = false;
    bool capture_scene = false;
    // Preserve this frame including UI for a subsequent screenshot() call.
    bool capture_output = false;
    unsigned bounds_budget = 32768;
};

struct FrameSampling {
    std::array<float, 2> jitter_pixels{};
    std::uint32_t shader_frame = 0;
    std::uint32_t taa_index = 0;
    std::uint32_t accumulation_index = 0;
    std::uint32_t random_counter = 0;
    std::uint32_t random_counter_secondary = 0;
    int path_sample_limit = 0;
};

struct ComputeExecution {
    std::string entry;
    std::uint64_t dispatches = 0;
};

struct PassTiming {
    std::string entry;
    double milliseconds = 0;
};

struct DeviceInfo {
    std::string name;
    std::string driver_name;
    std::string driver_info;
    std::string uuid;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t driver_version = 0;
    std::uint32_t api_version = 0;
    std::uint32_t queue_family = 0;
    std::uint32_t timestamp_bits = 0;
    float timestamp_period_nanoseconds = 0;
};

struct MemoryHeap {
    std::uint64_t size_bytes = 0;
    std::uint64_t budget_bytes = 0;
    std::uint64_t estimated_usage_bytes = 0;
    bool device_local = false;
};

struct MemoryStatistics {
    std::uint64_t renderer_allocated_bytes = 0;
    bool budget_supported = false;
    std::vector<MemoryHeap> heaps;
};

struct Statistics {
    std::string device;
    std::uint64_t allocated_bytes = 0;
    std::uint64_t entity_upload_bytes = 0;
    // Counts successful queue submissions, including a later presentation failure.
    std::uint64_t frame_count = 0;
    // Identifies the completed submission to which GPU timings belong.
    std::uint64_t gpu_frame_count = 0;
    std::uint64_t validation_errors = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t output_width = 0;
    std::uint32_t output_height = 0;
    double gpu_milliseconds = 0;
    double gpu_prepare_milliseconds = 0;
    double gpu_render_milliseconds = 0;
    double cpu_wait_milliseconds = 0;
    double cpu_record_milliseconds = 0;
    double cpu_submit_milliseconds = 0;
    std::uint32_t passes = 0;
    std::uint64_t world_compute_passes = 0;
    bool gpu_generated_world = false;
    bool gpu_timestamps_supported = false;
    bool world_updates_profiled = false;
    bool render_passes_profiled = false;
    ChunkLayout chunk_layout = ChunkLayout::split;
    PresentationMode presentation_mode = PresentationMode::automatic;
    // Selected after surface creation; actual submitted path afterward, including capture.
    bool direct_output = false;
    // Belongs to gpu_frame_count, including presentation work in the final pass.
    std::vector<PassTiming> gpu_passes;
};

class Renderer {
  public:
    [[nodiscard]] static std::expected<Renderer, std::string>
    create(platform::window::Window& window, Configuration configuration = {});
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    ~Renderer();
    // Acquires a drawable and starts ImGui. False means minimized/out-of-date.
    [[nodiscard]] std::expected<bool, std::string> begin_frame();
    // On success, sampling receives the submitted frame's shader parameters if requested.
    [[nodiscard]] std::expected<void, std::string> draw(const FrameInput& input,
                                                        FrameSampling* sampling = nullptr);
    [[nodiscard]] std::expected<world::model::GeneratedWorld, std::string>
    generate_world(const world::model::Model& source, world::model::Vec3i requested = {},
                   int segment_groups = 4);
    // Commits CPU and GPU world state together; adoption failure restores the prior scene.
    [[nodiscard]] std::expected<void, std::string>
    generate_world(world::model::World& target, const world::model::Model& source,
                   world::model::Vec3i requested = {}, int segment_groups = 4);
    [[nodiscard]] std::vector<ComputeExecution> world_executions() const;
    void reset_accumulation();
    // Reconstructs the selected renderer on the next draw, including a same-mode selection.
    void restart();
    // Predicts the next draw's slot, including mode reconstruction and P pause semantics.
    [[nodiscard]] unsigned history_index(settings::RenderVersion next_mode,
                                         bool paused = false) const;
    [[nodiscard]] Statistics statistics() const;
    [[nodiscard]] DeviceInfo device_info() const;
    // Driver heap usage is an estimate; renderer allocations exclude driver/UI overhead.
    [[nodiscard]] MemoryStatistics memory_statistics() const;
    // Waits for the final submitted frame and collects its timestamps; no new frame is acquired.
    [[nodiscard]] std::expected<void, std::string> finish();
    // The submitted frame must request capture_scene or capture_output before presentation.
    [[nodiscard]] std::expected<void, std::string> screenshot(const std::filesystem::path& path);

  private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::rendering::vulkan
#endif
