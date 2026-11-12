#ifndef ENGINE_RENDERING_VULKAN_SRC_PROGRAM_REFLECTION_HPP
#define ENGINE_RENDERING_VULKAN_SRC_PROGRAM_REFLECTION_HPP

#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace engine::rendering::vulkan {

struct Uniform {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t stride = 0;
};

struct Resource {
    std::string name;
    std::uint32_t binding = 0;
    vk::DescriptorType type = vk::DescriptorType::eStorageBuffer;
};

struct Reflection {
    std::map<std::string, Uniform, std::less<>> uniforms;
    std::vector<Resource> resources;
    std::uint32_t uniform_size = 0;
    std::string entry;
    vk::ShaderStageFlagBits stage = vk::ShaderStageFlagBits::eCompute;
};

[[nodiscard]] Reflection read_reflection(const std::filesystem::path& path);

} // namespace engine::rendering::vulkan
#endif
