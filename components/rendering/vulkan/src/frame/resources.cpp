#include "../state.hpp"
#include "../world/compute.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace engine::rendering::vulkan {

void Renderer::Impl::add_buffer(std::string name, vk::DeviceSize bytes) {
    if (bytes > device.properties.limits.maxStorageBufferRange) {
        throw std::runtime_error("Storage buffer exceeds this GPU's range: " + name);
    }
    const auto usage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eIndirectBuffer;
    buffers[name] = make_buffer(device, {bytes, usage, false});
    screen_buffers.push_back(std::move(name));
}

void Renderer::Impl::allocate_screen(settings::RenderVersion version) {
    for (const auto& name : screen_buffers) {
        buffers.erase(name);
    }
    screen_buffers.clear();
    const vk::DeviceSize pixels = vk::DeviceSize(render_extent.width) * render_extent.height;
    const vk::DeviceSize buckets =
        ((render_extent.width + 7u) / 8u) * ((render_extent.height + 7u) / 8u);
    add_buffer("firstHitData", pixels * 16);
    add_buffer("rayStepCount", pixels * 4);
    if (version != settings::RenderVersion::path_tracer) {
        add_buffer("taaSamples", pixels * 32 * 8);
        add_buffer("taaSampleAccum", pixels * 8);
    }
    if (version != settings::RenderVersion::albedo) {
        add_buffer("atmosphereComp", 1024u * 1024u * 12u);
        add_buffer("firstHitAbsorption", pixels * 8);
        add_buffer("finalColor", pixels * 8);
    }
    if (version == settings::RenderVersion::path_tracer) {
        add_buffer("sampleAccumulated", pixels * 16);
    }
    if (version == settings::RenderVersion::base) {
        allocate_gi(pixels, buckets);
    }
    resources_ready = true;
    reset = true;
}

void Renderer::Impl::allocate_gi(vk::DeviceSize pixels, vk::DeviceSize buckets) {
    add_buffer("pixelsToRender", (pixels + 1) * 4);
    add_buffer("groupCount", 20);
    add_buffer("taaDistMinMax", pixels * 8);
    add_buffer("denoisePreprocessed", pixels * 12);
    add_buffer("denoisePreprocessedHorizontal", pixels * 12);
    add_buffer("globalIlumBucketInfo", buckets * 8);
    add_buffer("globalIlumValidSamples", pixels * 2 * 32);
    add_buffer("globalIlumValidSamplesCompressed", buckets * 8 * 16);
    add_buffer("globalIlumValidSamplesRefined", buckets * 32 * 16);
    add_buffer("globalIlumInvalidSamples", pixels * 8 * 16);
    add_buffer("globalIlumSampleCounts", (128 + 3) * 8);
    add_buffer("globalIlumValidDispatch", 20);
    add_buffer("globalIlumInvalidDispatch", 20);
}

void Renderer::Impl::clear_screen() {
    for (const auto& name : screen_buffers) {
        const auto& buffer = *buffers.at(name);
        command.fillBuffer(*buffer.handle, 0, buffer.size, 0);
    }
    memory_barrier(command);
    const std::array<std::uint32_t, 5> arguments{0, 1, 1, 0, 0};
    for (const auto* name :
         {"groupCount", "globalIlumValidDispatch", "globalIlumInvalidDispatch"}) {
        const auto found = buffers.find(name);
        if (found != buffers.end()) {
            vkCmdUpdateBuffer(command, *found->second->handle, 0, sizeof(arguments),
                              arguments.data());
        }
    }
    history = {};
    memory_barrier(command);
    reset = false;
    force_path_reset = true;
}

void Renderer::Impl::prepare_resources(const FrameInput& input) {
    const auto requested = input.settings->render.version;
    if (!resources_ready || requested != mode || restart_requested) {
        allocate_screen(requested);
        mode = requested;
    }
    // Original Base/Albedo preserve temporal samples across ordinary world edits.
    // PathTracer restarts accumulation through maxSamples, not by clearing GI/TAA.
    if (reset) {
        clear_screen();
    }
    upload_world(input);
    memory_barrier(command);
}

std::uint64_t Renderer::Impl::allocated_bytes() const {
    std::uint64_t result = 0;
    for (const auto& [name, buffer] : buffers) {
        static_cast<void>(name);
        result += buffer->allocation;
    }
    for (const auto& buffer : uploads) {
        result += buffer->allocation;
    }
    for (const auto& [name, program] : programs) {
        static_cast<void>(name);
        result += program->uniform->allocation;
    }
    return result + world_compute->allocated_bytes() + chunks.allocation() +
           (output ? output->allocation : 0) + (scene_output ? scene_output->allocation : 0);
}

} // namespace engine::rendering::vulkan
