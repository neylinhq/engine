#include "values.hpp"

#include <algorithm>
#include <cmath>

namespace engine::rendering::vulkan {
namespace {
void camera_parameters(UniformValues& values, const FrameInput& input, const History& history) {
    const auto position = input.camera->pose().position;
    const Vector3 fraction{float(position.x - std::floor(position.x)),
                           float(position.y - std::floor(position.y)),
                           float(position.z - std::floor(position.z))};
    set_value(values, "camPosIntX", static_cast<std::int32_t>(std::floor(position.x)));
    set_value(values, "camPosIntY", static_cast<std::int32_t>(std::floor(position.y)));
    set_value(values, "camPosIntZ", static_cast<std::int32_t>(std::floor(position.z)));
    set_value(values, "camPosFrac", fraction);
    set_value(values, "camPos", Vector3{float(position.x), float(position.y), float(position.z)});
    set_value(values, "camMatrix", history.matrix);
    set_value(values, "invCamMatrix", history.inverse);
    set_value(values, "camRotOld", history.matrices);
    set_value(values, "taaOldCamPosFromCurCamInt", history.offsets);
    set_value(values, "taaJitterOld", history.jitters);
    set_value(values, "taaJitter", history.jitter);
    set_value(values, "boundingBoxMin", Vector3{0.1f, 0.1f, 0.1f});
    const auto size = input.world.size;
    set_value(values, "boundingBoxMax",
              Vector3{float(size.x) - 0.1f, float(size.y) - 0.1f, float(size.z) - 0.1f});
    set_value(values, "groupSizeX", size.x / 64);
    set_value(values, "groupSizeY", size.y / 64);
    set_value(values, "groupSizeZ", size.z / 64);
}
} // namespace

const UniformValues& Renderer::Impl::parameters(const FrameInput& input) {
    // Every frame populates the same keys and value types. Keep their storage stable
    // so program reflection can bind source vectors once, without per-frame allocation.
    auto& values = uniform_values;
    camera_parameters(values, input, history);
    atmosphere_parameters(values, input.settings->atmosphere);
    render_parameters(values, *input.settings);
    const auto frame = static_cast<std::uint32_t>(serial);
    const auto history_count =
        std::clamp(input.settings->render.base.global_illumination_max_accumulation, 1, 128);
    set_value(values, "frameCount", frame);
    set_value(values, "frameIndex", frame);
    set_value(values, "taaIndex", 127u - (frame % 128u));
    set_value(values, "taaFinalIndex", 0u);
    set_value(values, "taaFinalIndexTest", 0u);
    set_value(values, "accumIndex",
              std::uint32_t(history_count) - (frame % std::uint32_t(history_count)) - 1u);
    set_value(values, "randCounter", random_value(frame));
    set_value(values, "randCounter2", random_value(frame ^ 0x9e3779b9u));
    set_value(values, "screenWidth", render_extent.width);
    set_value(values, "screenHeight", render_extent.height);
    set_value(values, "outputWidth", surface_extent.width);
    set_value(values, "outputHeight", surface_extent.height);
    set_value(values, "globalIlumBucketSizeX", (render_extent.width + 7u) / 8u);
    set_value(values, "globalIlumBucketSizeY", (render_extent.height + 7u) / 8u);
    set_value(values, "globalIlumBucketCount",
              ((render_extent.width + 7u) / 8u) * ((render_extent.height + 7u) / 8u));
    set_value(values, "atmosphereTexSizeX", 1024);
    set_value(values, "atmosphereTexSizeY", 1024);
    const bool restart = input.camera->changed() || input.reset_accumulation || force_path_reset;
    set_value(values, "maxSamples",
              restart ? 1 : std::clamp(input.settings->render.path_tracer.max_samples, 1, 8192));
    return values;
}

} // namespace engine::rendering::vulkan
