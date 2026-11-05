#include "../frame/values.hpp"

#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
void render_barrier(vk::CommandBuffer command) {
    const auto stages = vk::PipelineStageFlagBits2::eComputeShader |
                        vk::PipelineStageFlagBits2::eDrawIndirect |
                        vk::PipelineStageFlagBits2::eFragmentShader;
    const auto access = vk::AccessFlagBits2::eShaderStorageRead |
                        vk::AccessFlagBits2::eShaderStorageWrite |
                        vk::AccessFlagBits2::eIndirectCommandRead;
    const vk::MemoryBarrier2 barrier(vk::PipelineStageFlagBits2::eComputeShader,
                                     vk::AccessFlagBits2::eShaderStorageWrite, stages, access);
    command.pipelineBarrier2(vk::DependencyInfo({}, barrier));
}

std::uint32_t random_slot(std::string_view name) {
    if (name.contains("GlobalIllum")) {
        return 1;
    }
    if (name.contains("SampleRefine")) {
        return 3;
    }
    if (name.contains("SpatialResampling")) {
        return 5;
    }
    return name.contains("Denoise") ? 6u : 0u;
}

void write_seed(std::byte* destination, const Reflection& reflection, std::string_view name,
                std::uint32_t seed) {
    const auto field = reflection.uniforms.find(name);
    if (field != reflection.uniforms.end()) {
        const auto value = random_value(seed) & 0x7fffffffu;
        std::memcpy(destination + field->second.offset, &value, sizeof(value));
    }
}
} // namespace

void Renderer::Impl::update_descriptors(Program& program) {
    const vk::DescriptorBufferInfo uniform(*program.uniform->handle, 0, program.uniform->size);
    const vk::WriteDescriptorSet uniform_write(
        program.descriptors, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniform);
    device.logical->updateDescriptorSets(uniform_write, {});
    for (const auto& resource : program.reflection.resources) {
        vk::WriteDescriptorSet write(program.descriptors, resource.binding, 0, 1, resource.type);
        if (resource.type == vk::DescriptorType::eSampledImage ||
            resource.type == vk::DescriptorType::eStorageImage) {
            const auto image = find_chunk_image(chunks, resource.name);
            if (!image) {
                throw std::runtime_error("Missing shader image: " + resource.name);
            }
            const vk::DescriptorImageInfo info({}, chunk_image_view(*image, resource.name),
                                               vk::ImageLayout::eGeneral);
            write.setImageInfo(info);
            device.logical->updateDescriptorSets(write, {});
        } else {
            const auto found = buffers.find(resource.name);
            if (found == buffers.end()) {
                throw std::runtime_error("Missing shader buffer: " + resource.name);
            }
            const auto& buffer = *found->second;
            const vk::DescriptorBufferInfo info(*buffer.handle, 0, buffer.size);
            write.setBufferInfo(info);
            device.logical->updateDescriptorSets(write, {});
        }
    }
}

void Renderer::Impl::write_uniforms(Program& program, const UniformValues& values) {
    auto* destination = static_cast<std::byte*>(program.uniform->mapped);
    std::memset(destination, 0, static_cast<std::size_t>(program.uniform->size));
    if (!program.uniforms_bound) {
        for (const auto& [name, field] : program.reflection.uniforms) {
            const auto found = values.find(name);
            if (found == values.end()) {
                continue;
            }
            if (found->second.size() > field.size) {
                throw std::runtime_error("Shader uniform ABI mismatch for " + name);
            }
            program.uniform_copies.push_back({field.offset, found->second.size(), &found->second});
        }
        program.uniforms_bound = true;
    }
    for (const auto& copy : program.uniform_copies) {
        if (copy.source->size() != copy.size) {
            throw std::runtime_error("Shader uniform type changed after reflection binding");
        }
        std::memcpy(destination + copy.offset, copy.source->data(), copy.size);
    }
    const auto old_matrices = program.reflection.uniforms.find("camRotOld");
    if (old_matrices != program.reflection.uniforms.end() &&
        program.name.starts_with("base_renderSampleRefine_")) {
        std::memcpy(destination + old_matrices->second.offset, history.inverses.data(),
                    sizeof(history.inverses));
    }
    const auto seed = static_cast<std::uint32_t>(serial) * 32u + random_slot(program.name);
    write_seed(destination, program.reflection, "randCounter", seed);
    write_seed(destination, program.reflection, "randCounter2", seed + 1u);
}

void Renderer::Impl::dispatch(const std::string& name, std::uint32_t groups,
                              const UniformValues& values) {
    auto& selected = program(name);
    write_uniforms(selected, values);
    update_descriptors(selected);
    command.bindPipeline(vk::PipelineBindPoint::eCompute, *selected.pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *selected.pipeline_layout, 0,
                               selected.descriptors, {});
    command.dispatch(groups, 1, 1);
    render_barrier(command);
    profile_pass(name);
    ++status.passes;
}

void Renderer::Impl::dispatch_indirect(const std::string& name, std::string_view buffer,
                                       const UniformValues& values) {
    auto& selected = program(name);
    write_uniforms(selected, values);
    update_descriptors(selected);
    command.bindPipeline(vk::PipelineBindPoint::eCompute, *selected.pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *selected.pipeline_layout, 0,
                               selected.descriptors, {});
    command.dispatchIndirect(*buffers.at(std::string(buffer))->handle, 0);
    render_barrier(command);
    profile_pass(name);
    ++status.passes;
}

} // namespace engine::rendering::vulkan
