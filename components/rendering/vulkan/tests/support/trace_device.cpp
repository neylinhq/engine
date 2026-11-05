#include "trace_probe.hpp"

#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan::test {
namespace {

platform::window::Window create_window() {
    auto result = platform::window::Window::create({32, 32, "NAADF ray contracts", false});
    if (!result) {
        throw std::runtime_error(result.error());
    }
    return std::move(*result);
}

void host_barrier(vk::CommandBuffer command) {
    vk::MemoryBarrier2 barrier;
    barrier.setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
        .setSrcAccessMask(vk::AccessFlagBits2::eShaderWrite)
        .setDstStageMask(vk::PipelineStageFlagBits2::eHost)
        .setDstAccessMask(vk::AccessFlagBits2::eHostRead);
    vk::DependencyInfo dependency;
    dependency.setMemoryBarriers(barrier);
    command.pipelineBarrier2(dependency);
}

} // namespace

TraceProbe::TraceProbe(const std::filesystem::path& binary, const std::filesystem::path& reflected,
                       ChunkLayout layout)
    : window(create_window()), device(window, true, layout),
      reflection(read_reflection(device.packed_chunks
                                     ? reflected.parent_path() / "packed" / reflected.filename()
                                     : reflected)) {
    create_descriptors();
    create_pipeline(device.packed_chunks ? binary.parent_path() / "packed" / binary.filename()
                                         : binary);
    command_pool = device.logical->createCommandPoolUnique(vk::CommandPoolCreateInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, device.family));
    command = device.logical
                  ->allocateCommandBuffers(vk::CommandBufferAllocateInfo(
                      *command_pool, vk::CommandBufferLevel::ePrimary, 1))
                  .front();
}

void TraceProbe::create_descriptors() {
    std::vector<vk::DescriptorSetLayoutBinding> bindings{
        {0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}};
    std::map<vk::DescriptorType, unsigned> counts{{vk::DescriptorType::eUniformBuffer, 1}};
    for (const auto& resource : reflection.resources) {
        bindings.emplace_back(resource.binding, resource.type, 1,
                              vk::ShaderStageFlagBits::eCompute);
        ++counts[resource.type];
    }
    descriptor_layout = device.logical->createDescriptorSetLayoutUnique(
        vk::DescriptorSetLayoutCreateInfo({}, bindings));
    std::vector<vk::DescriptorPoolSize> sizes;
    for (const auto& [type, count] : counts) {
        sizes.emplace_back(type, count);
    }
    descriptor_pool =
        device.logical->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo({}, 1, sizes));
    descriptors = device.logical
                      ->allocateDescriptorSets(
                          vk::DescriptorSetAllocateInfo(*descriptor_pool, *descriptor_layout))
                      .front();
}

void TraceProbe::create_pipeline(const std::filesystem::path& binary) {
    const auto words = read_program(binary);
    module = device.logical->createShaderModuleUnique(vk::ShaderModuleCreateInfo({}, words));
    pipeline_layout = device.logical->createPipelineLayoutUnique(
        vk::PipelineLayoutCreateInfo({}, *descriptor_layout));
    const vk::PipelineShaderStageCreateInfo stage({}, vk::ShaderStageFlagBits::eCompute, *module,
                                                  "main");
    pipeline = device.logical
                   ->createComputePipelineUnique(
                       {}, vk::ComputePipelineCreateInfo({}, stage, *pipeline_layout))
                   .value;
}

void TraceProbe::write_descriptors() {
    const auto& uniform = *buffers.at("uniform");
    const vk::DescriptorBufferInfo info(*uniform.handle, 0, uniform.size);
    device.logical->updateDescriptorSets(vk::WriteDescriptorSet(descriptors, 0, 0, 1,
                                                                vk::DescriptorType::eUniformBuffer,
                                                                nullptr, &info),
                                         {});
    for (const auto& resource : reflection.resources) {
        vk::WriteDescriptorSet write(descriptors, resource.binding, 0, 1, resource.type);
        if (const auto source = find_chunk_image(chunks, resource.name)) {
            const vk::DescriptorImageInfo image({}, chunk_image_view(*source, resource.name),
                                                vk::ImageLayout::eGeneral);
            write.setImageInfo(image);
            device.logical->updateDescriptorSets(write, {});
        } else {
            const auto& buffer = *buffers.at(resource.name);
            const vk::DescriptorBufferInfo data(*buffer.handle, 0, buffer.size);
            write.setBufferInfo(data);
            device.logical->updateDescriptorSets(write, {});
        }
    }
}

std::vector<RayRecord> TraceProbe::run(const world::model::World& world,
                                       std::span<const Ray> rays) {
    command.reset();
    command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    prepare_buffers(world, rays);
    upload_chunks(world);
    write_descriptors();
    command.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline_layout, 0, descriptors,
                               {});
    command.dispatch(static_cast<std::uint32_t>((rays.size() + 63) / 64), 1, 1);
    host_barrier(command);
    command.end();
    device.queue.submit(vk::SubmitInfo().setCommandBuffers(command));
    device.queue.waitIdle();
    if (device.validation_errors.load(std::memory_order_relaxed) != 0) {
        throw std::runtime_error("Vulkan validation failed in ray contract probe");
    }
    std::vector<RayRecord> result(rays.size());
    std::memcpy(result.data(), buffers.at("test_results")->mapped,
                result.size() * sizeof(RayRecord));
    return result;
}

} // namespace engine::rendering::vulkan::test
