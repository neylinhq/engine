#ifndef ENGINE_RENDERING_VULKAN_SRC_GPU_DEVICE_HPP
#define ENGINE_RENDERING_VULKAN_SRC_GPU_DEVICE_HPP

#include <atomic>
#include <engine/platform/window/window.hpp>
#include <engine/rendering/vulkan/renderer.hpp>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace engine::rendering::vulkan {

struct Surface {
    vk::Instance instance;
    vk::SurfaceKHR handle;
    ~Surface();
};

struct Messenger {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT handle = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy = nullptr;
    ~Messenger();
};

struct Device {
    std::atomic<std::uint64_t> validation_errors{0};
    vk::UniqueInstance instance;
    Messenger messenger;
    Surface surface;
    vk::PhysicalDevice physical;
    vk::PhysicalDeviceProperties properties;
    vk::UniqueDevice logical;
    vk::Queue queue;
    std::uint32_t family = 0;
    std::uint32_t timestamp_bits = 0;
    bool packed_chunks = false;
    bool memory_budget = false;
    Device(platform::window::Window& window, bool validation,
           ChunkLayout chunk_layout = ChunkLayout::automatic,
           const std::string& application_name = "Engine");
    ~Device();
    void create_instance(platform::window::Window& window, bool validation,
                         const std::string& application_name);
    void create_messenger();
    void select_physical();
    void create_logical();
    [[nodiscard]] std::uint32_t memory_type(std::uint32_t bits,
                                            vk::MemoryPropertyFlags required) const;
};

struct Buffer {
    vk::Device device;
    vk::UniqueDeviceMemory memory;
    vk::UniqueBuffer handle;
    vk::DeviceSize size = 0;
    vk::DeviceSize allocation = 0;
    void* mapped = nullptr;
    ~Buffer();
};

struct BufferDescription {
    vk::DeviceSize size = 4;
    vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer;
    bool host_visible = false;
};

struct Image {
    vk::UniqueDeviceMemory memory;
    vk::UniqueImage handle;
    vk::UniqueImageView view;
    vk::UniqueImageView atomic_view;
    vk::Extent3D extent;
    vk::Format format;
    vk::DeviceSize allocation = 0;
};

struct ChunkImages {
    std::shared_ptr<Image> geometry;
    std::shared_ptr<Image> entities;
    [[nodiscard]] bool packed() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::uint64_t allocation() const;
};

struct ImageDescription {
    vk::Extent3D extent;
    vk::Format format = vk::Format::eR32Uint;
    vk::ImageUsageFlags usage;
    bool volume = false;
    vk::ImageCreateFlags flags = {};
    std::span<const vk::Format> view_formats = {};
};

inline std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] std::shared_ptr<Buffer> make_buffer(Device& device, BufferDescription description);
[[nodiscard]] std::shared_ptr<Image> make_image(Device& device, ImageDescription description);
[[nodiscard]] ChunkImages make_chunk_images(Device& device, vk::Extent3D extent);
[[nodiscard]] bool select_chunk_layout(vk::PhysicalDevice physical, ChunkLayout requested);
[[nodiscard]] std::shared_ptr<Image> find_chunk_image(const ChunkImages& images,
                                                      std::string_view name);
[[nodiscard]] vk::ImageView chunk_image_view(const Image& image, std::string_view name);
[[nodiscard]] std::vector<std::uint32_t> read_program(const std::filesystem::path& path);
void memory_barrier(vk::CommandBuffer command);
void image_barrier(vk::CommandBuffer command, vk::Image image, vk::ImageLayout before,
                   vk::ImageLayout after);

} // namespace engine::rendering::vulkan
#endif
