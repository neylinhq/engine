#include "../state.hpp"

#include <algorithm>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
std::vector<vk::DescriptorSetLayoutBinding> layout_bindings(const Reflection& reflection) {
    const auto stages = reflection.stage == vk::ShaderStageFlagBits::eCompute
                            ? vk::ShaderStageFlags(vk::ShaderStageFlagBits::eCompute)
                            : vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    std::vector<vk::DescriptorSetLayoutBinding> result{
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, stages)};
    for (const auto& resource : reflection.resources) {
        result.emplace_back(resource.binding, resource.type, 1, stages);
    }
    return result;
}

std::vector<vk::DescriptorPoolSize>
pool_sizes(std::span<const vk::DescriptorSetLayoutBinding> bindings) {
    std::vector<vk::DescriptorPoolSize> sizes;
    for (const auto& binding : bindings) {
        const auto existing =
            std::ranges::find(sizes, binding.descriptorType, &vk::DescriptorPoolSize::type);
        if (existing == sizes.end()) {
            sizes.emplace_back(binding.descriptorType, 1);
        } else {
            ++existing->descriptorCount;
        }
    }
    return sizes;
}

void create_layout(Device& device, Program& program) {
    const auto bindings = layout_bindings(program.reflection);
    const auto storage_count = std::ranges::count_if(bindings, [](const auto& binding) {
        return binding.descriptorType == vk::DescriptorType::eStorageBuffer;
    });
    if (storage_count > device.properties.limits.maxPerStageDescriptorStorageBuffers) {
        throw std::runtime_error("GPU storage descriptor limit is too small for " + program.name);
    }
    if (program.reflection.uniform_size > device.properties.limits.maxUniformBufferRange) {
        throw std::runtime_error("GPU uniform range is too small for " + program.name);
    }
    program.descriptor_layout = device.logical->createDescriptorSetLayoutUnique(
        vk::DescriptorSetLayoutCreateInfo({}, bindings));
    program.pipeline_layout = device.logical->createPipelineLayoutUnique(
        vk::PipelineLayoutCreateInfo({}, *program.descriptor_layout));
    const auto sizes = pool_sizes(bindings);
    program.pool =
        device.logical->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo({}, 1, sizes));
    program.descriptors = device.logical
                              ->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(
                                  *program.pool, *program.descriptor_layout))
                              .front();
    program.uniform = make_buffer(
        device, {program.reflection.uniform_size, vk::BufferUsageFlagBits::eUniformBuffer, true});
}

vk::UniqueShaderModule load_module(Device& device, const std::filesystem::path& path) {
    const auto words = read_program(path);
    return device.logical->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, words.size() * sizeof(std::uint32_t), words.data()));
}

void compute_pipeline(Device& device, Program& program, const std::filesystem::path& path) {
    const auto module = load_module(device, path);
    const vk::PipelineShaderStageCreateInfo stage({}, vk::ShaderStageFlagBits::eCompute, *module,
                                                  "main");
    program.pipeline =
        device.logical
            ->createComputePipelineUnique(
                {}, vk::ComputePipelineCreateInfo({}, stage, *program.pipeline_layout))
            .value;
}

struct GraphicsState {
    vk::PipelineVertexInputStateCreateInfo input;
    vk::PipelineInputAssemblyStateCreateInfo assembly;
    vk::PipelineViewportStateCreateInfo viewport;
    vk::PipelineRasterizationStateCreateInfo raster;
    vk::PipelineMultisampleStateCreateInfo samples;
    vk::PipelineColorBlendAttachmentState attachment;
    vk::PipelineColorBlendStateCreateInfo blend;
    std::array<vk::DynamicState, 2> dynamics{vk::DynamicState::eViewport,
                                             vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic;
    vk::Format color_format;
    vk::PipelineRenderingCreateInfo rendering;

    explicit GraphicsState(vk::Format format) : color_format(format) {
        assembly.setTopology(vk::PrimitiveTopology::eTriangleList);
        viewport.setViewportCount(1).setScissorCount(1);
        raster.setPolygonMode(vk::PolygonMode::eFill)
            .setCullMode(vk::CullModeFlagBits::eNone)
            .setLineWidth(1);
        samples.setRasterizationSamples(vk::SampleCountFlagBits::e1);
        attachment.setColorWriteMask(
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
        blend.setAttachments(attachment);
        dynamic.setDynamicStates(dynamics);
        rendering.setColorAttachmentFormats(color_format);
    }
};

void graphics_pipeline(Device& device, Program& program, const std::filesystem::path& directory,
                       vk::Format format) {
    auto vertex_name = program.name;
    vertex_name.replace(vertex_name.size() - 6, 6, "MainVS");
    const auto vertex = load_module(device, directory / (vertex_name + ".spv"));
    const auto fragment = load_module(device, directory / (program.name + ".spv"));
    const std::array stages{
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertex, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragment,
                                          "main")};
    GraphicsState state(format);
    vk::GraphicsPipelineCreateInfo info;
    info.setPNext(&state.rendering)
        .setStages(stages)
        .setPVertexInputState(&state.input)
        .setPInputAssemblyState(&state.assembly)
        .setPViewportState(&state.viewport)
        .setPRasterizationState(&state.raster)
        .setPMultisampleState(&state.samples)
        .setPColorBlendState(&state.blend)
        .setPDynamicState(&state.dynamic)
        .setLayout(*program.pipeline_layout);
    program.pipeline = device.logical->createGraphicsPipelineUnique({}, info).value;
}
} // namespace

std::unique_ptr<Program> make_program(Device& device, const std::filesystem::path& directory,
                                      const std::string& name, vk::Format format) {
    const auto shader_directory = device.packed_chunks ? directory / "packed" : directory;
    auto result = std::make_unique<Program>();
    result->name = name;
    result->reflection = read_reflection(shader_directory / (name + ".json"));
    create_layout(device, *result);
    if (result->reflection.stage == vk::ShaderStageFlagBits::eCompute) {
        compute_pipeline(device, *result, shader_directory / (name + ".spv"));
    } else {
        graphics_pipeline(device, *result, shader_directory, format);
    }
    return result;
}

Program& Renderer::Impl::program(const std::string& name) {
    const auto found = programs.find(name);
    if (found != programs.end()) {
        return *found->second;
    }
    auto value = make_program(device, configuration.shaders, name, color_format);
    auto& result = *value;
    programs.emplace(name, std::move(value));
    return result;
}

} // namespace engine::rendering::vulkan
