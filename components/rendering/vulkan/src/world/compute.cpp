#include "compute.hpp"

#include <limits>
#include <set>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {

std::uint64_t retained_image_bytes(const ComputeProgram& program, std::set<const Image*>& seen) {
    std::uint64_t bytes = 0;
    for (const auto& binding : program.bindings) {
        for (const auto& image : binding->images) {
            if (seen.insert(image.get()).second) {
                bytes += image->allocation;
            }
        }
    }
    return bytes;
}

} // namespace

WorldCompute::WorldCompute(Device& device, std::filesystem::path shaders, BufferMap& buffers,
                           ChunkImages& chunks)
    : device(device), buffers(buffers), chunks(chunks), shaders_(std::move(shaders)) {
    pool_ = device.logical->createCommandPoolUnique(
        {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, device.family});
    internal_command_ =
        device.logical->allocateCommandBuffers({*pool_, vk::CommandBufferLevel::ePrimary, 1})
            .front();
    fence_ = device.logical->createFenceUnique({});
}

WorldCompute::~WorldCompute() {
    try {
        synchronize();
    } catch (const std::exception&) {
    }
}

vk::CommandBuffer WorldCompute::command() {
    if (external_command_) {
        return external_command_;
    }
    if (pending_) {
        throw std::logic_error("World compute submission must complete before recording");
    }
    if (!recording_) {
        internal_command_.reset();
        internal_command_.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        recording_ = true;
    }
    return internal_command_;
}

void WorldCompute::synchronize() {
    if (external_command_) {
        throw std::logic_error("Cannot synchronize an attached frame command buffer");
    }
    if (!recording_ && !pending_) {
        return;
    }
    if (recording_) {
        internal_command_.end();
        recording_ = false;
        device.logical->resetFences(*fence_);
        const vk::CommandBufferSubmitInfo commands(internal_command_);
        vk::SubmitInfo2 submit;
        submit.setCommandBufferInfos(commands);
        device.queue.submit2(submit, *fence_);
        pending_ = true;
    }
    const auto result =
        device.logical->waitForFences(*fence_, true, std::numeric_limits<std::uint64_t>::max());
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("World compute submission did not complete");
    }
    pending_ = false;
    release_completed();
}

void WorldCompute::cancel() {
    if (external_command_) {
        throw std::logic_error("Cannot cancel an attached frame command buffer");
    }
    if (pending_) {
        synchronize();
    }
    internal_command_.reset();
    recording_ = false;
    release_completed();
}

void WorldCompute::attach(vk::CommandBuffer command) {
    if (external_command_) {
        throw std::logic_error("World compute is already attached to a frame");
    }
    synchronize();
    external_command_ = command;
}

void WorldCompute::detach() {
    external_command_ = nullptr;
}

void WorldCompute::release_completed() {
    if (pending_ || recording_ || external_command_) {
        throw std::logic_error("Cannot release unfinished world compute resources");
    }
    for (auto& [name, program] : programs_) {
        static_cast<void>(name);
        // Uniform storage and descriptor sets are reusable only after completion.
        // Resource ownership must end here so cached slots do not retain old worlds.
        for (auto& binding : program.bindings) {
            binding->buffers.clear();
            binding->images.clear();
        }
        program.used_bindings = 0;
    }
    retained_.clear();
}

const std::map<std::string, std::uint64_t>& WorldCompute::execution_counts() const {
    return executions_;
}

std::uint64_t WorldCompute::allocated_bytes() const {
    std::uint64_t bytes = 0;
    std::set<const Buffer*> seen;
    for (const auto& [name, buffer] : buffers) {
        static_cast<void>(name);
        seen.insert(buffer.get());
    }
    const auto add = [&](const std::shared_ptr<Buffer>& buffer) {
        if (seen.insert(buffer.get()).second) {
            bytes += buffer->allocation;
        }
    };
    std::set<const Image*> images{chunks.geometry.get(), chunks.entities.get()};
    for (const auto& [name, program] : programs_) {
        static_cast<void>(name);
        add(program.pipeline->uniform);
        for (const auto& binding : program.bindings) {
            add(binding->uniform);
            for (const auto& buffer : binding->buffers) {
                add(buffer);
            }
        }
        bytes += retained_image_bytes(program, images);
    }
    for (const auto& buffer : retained_) {
        add(buffer);
    }
    return bytes;
}

std::uint64_t WorldCompute::binding_allocations() const {
    std::uint64_t count = 0;
    for (const auto& [name, program] : programs_) {
        static_cast<void>(name);
        count += program.bindings.size();
    }
    return count;
}

ComputeProgram& WorldCompute::program(const std::string& name) {
    const auto found = programs_.find(name);
    if (found != programs_.end()) {
        return found->second;
    }
    auto value = make_program(device, shaders_, name, vk::Format::eUndefined);
    return programs_.emplace(name, ComputeProgram{std::move(value), {}, 0}).first->second;
}

ComputeBinding& WorldCompute::bind(ComputeProgram& program, const UniformValues& values) {
    const auto commands = command();
    auto& pipeline = *program.pipeline;
    if (program.used_bindings == program.bindings.size()) {
        program.bindings.push_back(make_compute_binding(*this, pipeline));
    }
    auto& result = *program.bindings[program.used_bindings];
    update_compute_binding(*this, pipeline, result, values);
    ++program.used_bindings;
    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline.pipeline);
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline.pipeline_layout, 0,
                                result.descriptors, {});
    return result;
}

void WorldCompute::run(const std::string& name, vk::Extent3D groups, const UniformValues& values) {
    if (groups.width == 0 || groups.height == 0 || groups.depth == 0) {
        return;
    }
    const auto& maximum = device.properties.limits.maxComputeWorkGroupCount;
    if (groups.width > maximum[0] || groups.height > maximum[1] || groups.depth > maximum[2]) {
        throw std::runtime_error("World dispatch exceeds GPU limits: " + name);
    }
    static_cast<void>(bind(program(name), values));
    command().dispatch(groups.width, groups.height, groups.depth);
    memory_barrier(command());
    ++executions_[name];
}

void WorldCompute::run_indirect(const std::string& name, const std::string& arguments,
                                const UniformValues& values) {
    const auto buffer = buffers.at(arguments);
    if (buffer->size < sizeof(vk::DispatchIndirectCommand)) {
        throw std::runtime_error("World indirect arguments require three dimensions");
    }
    retained_.push_back(buffer);
    static_cast<void>(bind(program(name), values));
    command().dispatchIndirect(*buffer->handle, 0);
    memory_barrier(command());
    ++executions_[name];
}

} // namespace engine::rendering::vulkan
