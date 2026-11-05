#include "../state.hpp"

#include <stdexcept>

namespace engine::rendering::vulkan {

std::uint32_t Renderer::Impl::timestamp_capacity() const {
    if (configuration.profile_render_passes) {
        return 32;
    }
    return configuration.profile_world_updates ? 3U : 2U;
}

void Renderer::Impl::begin_timing() {
    timestamp_count = 0;
    timed_passes.clear();
    if (timestamps) {
        command.resetQueryPool(*timestamps, 0, timestamp_capacity());
        command.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestamps,
                                timestamp_count++);
    }
}

void Renderer::Impl::prepared_timestamp() {
    if (timestamps && status.world_updates_profiled) {
        command.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestamps,
                                timestamp_count++);
    }
}

void Renderer::Impl::profile_pass(std::string_view name) {
    if (!timestamps || !configuration.profile_render_passes) {
        return;
    }
    if (timestamp_count >= timestamp_capacity()) {
        throw std::length_error("Render pass timestamp capacity exceeded");
    }
    timed_passes.emplace_back(name);
    command.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestamps,
                            timestamp_count++);
}

void Renderer::Impl::end_timing() {
    if (timestamps && !configuration.profile_render_passes) {
        command.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestamps,
                                timestamp_count++);
    }
}

void Renderer::Impl::collect_timings(std::span<const std::uint64_t> times) {
    const auto mask = device.timestamp_bits >= 64
                          ? ~std::uint64_t{0}
                          : ((std::uint64_t{1} << device.timestamp_bits) - 1);
    const auto milliseconds = [&](std::uint64_t begin, std::uint64_t end) {
        return double((end - begin) & mask) * device.properties.limits.timestampPeriod * 0.000001;
    };
    status.gpu_milliseconds = milliseconds(times.front(), times.back());
    status.gpu_frame_count = status.frame_count;
    if (status.world_updates_profiled) {
        status.gpu_prepare_milliseconds = milliseconds(times[0], times[1]);
        status.gpu_render_milliseconds = milliseconds(times[1], times.back());
    }
    status.gpu_passes.clear();
    for (std::size_t index = 0; index < timed_passes.size(); ++index) {
        status.gpu_passes.push_back(
            {timed_passes[index], milliseconds(times[index + 1], times[index + 2])});
    }
}

void Renderer::Impl::collect() {
    if (!submitted || !timestamps || timestamp_count == 0) {
        return;
    }
    std::array<std::uint64_t, 32> times{};
    const auto result = vkGetQueryPoolResults(*device.logical, *timestamps, 0, timestamp_count,
                                              timestamp_count * sizeof(std::uint64_t), times.data(),
                                              sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result == VK_SUCCESS) {
        collect_timings(std::span(times).first(timestamp_count));
    }
}

} // namespace engine::rendering::vulkan
