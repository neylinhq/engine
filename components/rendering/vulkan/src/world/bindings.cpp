#include "compute.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {

std::vector<vk::DescriptorPoolSize> pool_sizes(const Reflection& reflection) {
    std::vector<vk::DescriptorPoolSize> sizes{{vk::DescriptorType::eUniformBuffer, 1}};
    for (const auto& resource : reflection.resources) {
        const auto found = std::ranges::find(sizes, resource.type, &vk::DescriptorPoolSize::type);
        if (found == sizes.end()) {
            sizes.emplace_back(resource.type, 1);
        } else {
            ++found->descriptorCount;
        }
    }
    return sizes;
}

void fill_uniform(Buffer& buffer, const Reflection& reflection, const UniformValues& values) {
    auto* data = static_cast<std::byte*>(buffer.mapped);
    std::memset(data, 0, static_cast<std::size_t>(buffer.size));
    for (const auto& [name, field] : reflection.uniforms) {
        if (field.offset > buffer.size || field.size > buffer.size - field.offset) {
            throw std::runtime_error("World uniform exceeds its allocation: " + name);
        }
        const auto found = values.find(name);
        if (found == values.end()) {
            continue;
        }
        if (found->second.size() > field.size) {
            throw std::runtime_error("World uniform ABI mismatch: " + name);
        }
        std::memcpy(data + field.offset, found->second.data(), found->second.size());
    }
}

void write_resource(WorldCompute& context, ComputeBinding& binding, const Resource& resource) {
    vk::WriteDescriptorSet write(binding.descriptors, resource.binding, 0, 1, resource.type);
    if (resource.type == vk::DescriptorType::eStorageImage ||
        resource.type == vk::DescriptorType::eSampledImage) {
        const auto image = find_chunk_image(context.chunks, resource.name);
        if (!image) {
            throw std::runtime_error("Missing world compute image: " + resource.name);
        }
        binding.images.push_back(image);
        const vk::DescriptorImageInfo info({}, chunk_image_view(*image, resource.name),
                                           vk::ImageLayout::eGeneral);
        write.setImageInfo(info);
        context.device.logical->updateDescriptorSets(write, {});
    } else {
        const auto found = context.buffers.find(resource.name);
        if (found == context.buffers.end()) {
            throw std::runtime_error("Missing world compute buffer: " + resource.name);
        }
        binding.buffers.push_back(found->second);
        const vk::DescriptorBufferInfo buffer(*found->second->handle, 0, found->second->size);
        write.setBufferInfo(buffer);
        context.device.logical->updateDescriptorSets(write, {});
    }
}

} // namespace

std::unique_ptr<ComputeBinding> make_compute_binding(WorldCompute& context, Program& program) {
    auto result = std::make_unique<ComputeBinding>();
    auto& device = context.device;
    result->uniform = make_buffer(
        device, {program.reflection.uniform_size, vk::BufferUsageFlagBits::eUniformBuffer, true});
    const auto sizes = pool_sizes(program.reflection);
    result->pool = device.logical->createDescriptorPoolUnique({{}, 1, sizes});
    result->descriptors =
        device.logical->allocateDescriptorSets({*result->pool, *program.descriptor_layout}).front();
    const vk::DescriptorBufferInfo uniform(*result->uniform->handle, 0, result->uniform->size);
    const vk::WriteDescriptorSet write(result->descriptors, 0, 0, 1,
                                       vk::DescriptorType::eUniformBuffer, nullptr, &uniform);
    device.logical->updateDescriptorSets(write, {});
    return result;
}

void update_compute_binding(WorldCompute& context, Program& program, ComputeBinding& binding,
                            const UniformValues& values) {
    fill_uniform(*binding.uniform, program.reflection, values);
    binding.buffers.clear();
    binding.images.clear();
    for (const auto& resource : program.reflection.resources) {
        write_resource(context, binding, resource);
    }
}

} // namespace engine::rendering::vulkan
