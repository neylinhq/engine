#ifndef ENGINE_RENDERING_VULKAN_STATE_HPP
#define ENGINE_RENDERING_VULKAN_STATE_HPP

#include "gpu/device.hpp"
#include "program/reflection.hpp"
#include <array>
#include <engine/rendering/vulkan/renderer.hpp>
#include <map>
#include <optional>
#include <span>
#include <string_view>

struct ImGuiContext;

namespace engine::rendering::vulkan {

using Matrix = std::array<float, 16>;
using Vector2 = std::array<float, 2>;
using Vector3 = std::array<float, 3>;
using Bytes = std::vector<std::byte>;
using UniformValues = std::map<std::string, Bytes, std::less<>>;
class WorldCompute;
class WorldUpdates;

struct UniformCopy {
    std::size_t offset;
    std::size_t size;
    const Bytes* source;
};

struct Program {
    std::string name;
    Reflection reflection;
    std::shared_ptr<Buffer> uniform;
    vk::UniqueDescriptorSetLayout descriptor_layout;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniqueDescriptorPool pool;
    vk::DescriptorSet descriptors;
    vk::UniquePipeline pipeline;
    std::vector<UniformCopy> uniform_copies;
    bool uniforms_bound = false;
};

struct History {
    std::array<Matrix, 128> matrices{};
    std::array<Matrix, 128> inverses{};
    std::array<foundation::geometry::Vec3, 128> positions{};
    std::array<Vector3, 128> offsets{};
    std::array<Vector2, 128> jitters{};
    Matrix matrix{};
    Matrix inverse{};
    Vector2 jitter{};
};

struct Renderer::Impl {
    platform::window::Window& window;
    Configuration configuration;
    Device device;
    vk::UniqueCommandPool command_pool;
    vk::CommandBuffer command;
    vk::UniqueFence completion;
    std::array<vk::UniqueSemaphore, 2> available;
    vk::UniqueQueryPool timestamps;
    std::uint32_t timestamp_count = 0;
    std::vector<std::string> timed_passes;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> swap_images;
    std::vector<vk::UniqueImageView> swap_views;
    std::vector<vk::UniqueSemaphore> finished;
    vk::Extent2D surface_extent;
    vk::Extent2D render_extent;
    vk::Format color_format = vk::Format::eUndefined;
    VkFormat ui_format = VK_FORMAT_UNDEFINED;
    std::uint32_t image_index = 0;
    std::uint32_t minimum_images = 2;
    std::shared_ptr<Image> output;
    std::shared_ptr<Image> scene_output;
    ChunkImages chunks;
    std::map<std::string, std::shared_ptr<Buffer>, std::less<>> buffers;
    std::vector<std::shared_ptr<Buffer>> uploads;
    std::vector<std::string> screen_buffers;
    std::map<std::string, std::unique_ptr<Program>, std::less<>> programs;
    std::unique_ptr<WorldCompute> world_compute;
    std::unique_ptr<WorldUpdates> world_updates;
    History history;
    UniformValues uniform_values;
    std::uint64_t serial = 0;
    std::uint64_t world_revision = ~std::uint64_t{0};
    std::uint64_t geometry_revision = ~std::uint64_t{0};
    std::uint64_t material_revision = ~std::uint64_t{0};
    std::uint64_t chunk_revision = ~std::uint64_t{0};
    std::uint64_t entity_revision = ~std::uint64_t{0};
    std::uint64_t entity_voxel_revision = ~std::uint64_t{0};
    settings::RenderVersion mode = settings::RenderVersion::base;
    Statistics status;
    ImGuiContext* context = nullptr;
    bool ui_platform = false;
    bool ui_vulkan = false;
    bool acquired = false;
    bool submitted = false;
    bool resize = true;
    bool reset = true;
    bool restart_requested = false;
    bool resources_ready = false;
    bool output_ready = false;
    bool direct_output = false;
    bool direct_frame = false;
    bool scene_ready = false;
    bool chunks_ready = false;
    bool force_path_reset = true;
    bool gpu_world = false;
    Impl(platform::window::Window& window, Configuration configuration);
    ~Impl();
    void initialize_commands();
    void complete_frame();
    [[nodiscard]] vk::Semaphore acquisition_semaphore() const;
    void rebuild_surface();
    void replace_swapchain(const vk::SurfaceCapabilitiesKHR& capabilities,
                           const vk::SurfaceFormatKHR& format);
    void bind_present_images(vk::SwapchainKHR replacement, vk::Format format);
    [[nodiscard]] vk::ImageUsageFlags
    configure_output(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Format format);
    void resize_output();
    void ensure_output();
    void prepare_output(const FrameInput& input);
    [[nodiscard]] vk::Image output_image() const;
    [[nodiscard]] vk::ImageView output_view() const;
    void initialize_ui();
    void shutdown_ui_renderer();
    bool begin_frame();
    void draw(const FrameInput& input);
    void submit();
    void collect();
    [[nodiscard]] std::uint32_t timestamp_capacity() const;
    void begin_timing();
    void prepared_timestamp();
    void profile_pass(std::string_view name);
    void end_timing();
    void collect_timings(std::span<const std::uint64_t> times);
    void prepare_resources(const FrameInput& input);
    void allocate_screen(settings::RenderVersion version);
    void allocate_gi(vk::DeviceSize pixels, vk::DeviceSize buckets);
    void add_buffer(std::string name, vk::DeviceSize bytes);
    void clear_screen();
    void upload_world(const FrameInput& input);
    void update_gpu_world(const FrameInput& input);
    [[nodiscard]] world::model::GeneratedWorld
    generate_world(const world::model::Model& source, world::model::IVec3 requested,
                   int segment_groups, world::model::World* target = nullptr);
    void upload_entities(const FrameInput& input);
    void upload_chunks(const FrameInput& input);
    void upload_chunk_image(const Image& image, const Buffer& staging, vk::DeviceSize offset);
    void upload_buffer(std::string_view name, std::span<const std::byte> data);
    void copy_to_buffer(Buffer& target, std::span<const std::byte> data, vk::DeviceSize offset = 0);
    void update_history(const FrameInput& input);
    [[nodiscard]] const UniformValues& parameters(const FrameInput& input);
    [[nodiscard]] Program& program(const std::string& name);
    void update_descriptors(Program& program);
    void write_uniforms(Program& program, const UniformValues& values);
    void dispatch(const std::string& name, std::uint32_t groups, const UniformValues& values);
    void dispatch_indirect(const std::string& name, std::string_view buffer,
                           const UniformValues& values);
    void record_base(const FrameInput& input, const UniformValues& values);
    void record_albedo(const FrameInput& input, const UniformValues& values);
    void record_path_tracer(const UniformValues& values);
    void record_original(const FrameInput& input, const UniformValues& values);
    void begin_output(vk::AttachmentLoadOp load);
    void record_final(const UniformValues& values, const FrameInput& input);
    void present_output();
    void copy_scene();
    void capture(const std::filesystem::path& path);
    [[nodiscard]] std::uint64_t allocated_bytes() const;
};

[[nodiscard]] std::unique_ptr<Program> make_program(Device& device,
                                                    const std::filesystem::path& directory,
                                                    const std::string& name, vk::Format format);
[[nodiscard]] Matrix view_projection(const motion::camera::Camera& camera, float aspect);
[[nodiscard]] Matrix inverse_matrix(const Matrix& matrix);
[[nodiscard]] Vector3 sunlight(const settings::Atmosphere& settings);
[[nodiscard]] Vector3 sun_direction(const settings::Atmosphere& settings);
void atmosphere_parameters(UniformValues& values, const settings::Atmosphere& settings);
void render_parameters(UniformValues& values, const settings::Settings& settings);
void write_png(const std::filesystem::path& path, vk::Extent2D extent,
               std::span<const std::byte> pixels, bool blue_first);

} // namespace engine::rendering::vulkan
#endif
