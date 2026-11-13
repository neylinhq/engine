#include "application.hpp"

#include "io/path.hpp"

#include <algorithm>
#include <boost/json.hpp>
#include <engine/platform/process/process.hpp>
#include <engine/rendering/configuration/configuration.hpp>
#include <span>
#include <stdexcept>
#include <vector>

namespace engine::app::sandbox {
namespace {
namespace json = boost::json;
namespace vulkan = rendering::vulkan;

json::value source_path(const std::filesystem::path& path) {
    if (path.empty()) {
        return nullptr;
    }
    return json::value(path_to_utf8(std::filesystem::absolute(path).lexically_normal()));
}

json::array coordinates(auto position) {
    return {position.x, position.y, position.z};
}

const char* render_mode(rendering::settings::RenderVersion mode) {
    using enum rendering::settings::RenderVersion;
    switch (mode) {
    case albedo:
        return "albedo";
    case base:
        return "base";
    case path_tracer:
        return "path_tracer";
    }
    return "unknown";
}

const char* presentation_mode(vulkan::PresentationMode mode) {
    using enum vulkan::PresentationMode;
    switch (mode) {
    case automatic:
        return "automatic";
    case immediate:
        return "immediate";
    case mailbox:
        return "mailbox";
    case fifo:
        return "fifo";
    }
    return "unknown";
}

std::string shader_package_hash(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto extension = entry.path().extension();
        if (entry.is_regular_file() && (extension == ".spv" || extension == ".json")) {
            paths.push_back(entry.path());
        }
    }
    if (paths.empty()) {
        throw std::runtime_error("No compiled shaders found");
    }
    std::ranges::sort(paths);
    json::array files;
    for (const auto& path : paths) {
        files.emplace_back(json::object{{"name", path_to_utf8(path.filename())},
                                        {"sha256", platform::process::file_sha256(path)}});
    }
    const auto manifest = json::serialize(files);
    return platform::process::sha256(std::as_bytes(std::span(manifest)));
}

json::object shader_package(const std::filesystem::path& directory, vulkan::ChunkLayout layout) {
    const auto selected = layout == vulkan::ChunkLayout::packed ? directory / "packed" : directory;
    json::object result{{"directory", source_path(selected)}};
    try {
        result["disk_sha256"] = shader_package_hash(selected);
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    return result;
}

json::object build_context() {
    json::object result{{"version", ENGINE_APP_SANDBOX_VERSION},
                        {"configuration", ENGINE_APP_SANDBOX_BUILD_TYPE},
                        {"compiler", ENGINE_APP_SANDBOX_COMPILER}};
    try {
        const auto executable = platform::process::executable();
        result["executable_sha256"] = platform::process::file_sha256(executable);
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    return result;
}

json::object device_context(const vulkan::DeviceInfo& device) {
    return {{"name", device.name},
            {"vendor_id", device.vendor_id},
            {"device_id", device.device_id},
            {"driver_name", device.driver_name},
            {"driver_info", device.driver_info},
            {"driver_version_raw", device.driver_version},
            {"api_version_raw", device.api_version}};
}

json::object camera_context(const motion::camera::Camera& camera, double voxel_meters) {
    const auto pose = camera.pose();
    return {{"position_m", coordinates(pose.position * voxel_meters)},
            {"yaw_radians", pose.yaw},
            {"pitch_radians", pose.pitch},
            {"vertical_fov_degrees", camera.settings().fov_degrees}};
}

json::object world_context(const vulkan::FrameInput& input, const SceneSource& source) {
    const auto& world = input.world;
    const auto& entities = *input.entities;
    return {{"source", source_path(source.world)},
            {"materials_source", source_path(source.materials)},
            {"size_voxels", coordinates(world.actual_size)},
            {"padded_size_voxels", coordinates(world.size)},
            {"revision", world.revision},
            {"geometry_revision", world.geometry_revision},
            {"chunk_revision", world.chunk_revision},
            {"material_revision", world.material_revision},
            {"entity_revision", entities.revision},
            {"entity_voxel_revision", entities.voxel_revision},
            {"entity_count", entities.entity_count},
            {"material_count", world.materials.size()},
            {"packed_materials_sha256", platform::process::sha256(std::as_bytes(world.materials))},
            {"chunk_words", world.chunks.size()},
            {"block_words", world.blocks.size()},
            {"voxel_words", world.voxels.size()}};
}

json::object timing_context(const vulkan::Statistics& statistics) {
    const auto gpu =
        statistics.gpu_timestamps_supported && statistics.gpu_frame_count == statistics.frame_count
            ? json::value(statistics.gpu_milliseconds)
            : json::value(nullptr);
    return {{"gpu_ms", gpu},
            {"cpu_fence_wait_ms", statistics.cpu_wait_milliseconds},
            {"cpu_record_ms", statistics.cpu_record_milliseconds},
            {"cpu_submit_and_present_ms", statistics.cpu_submit_milliseconds}};
}

json::object sampling_context(const vulkan::FrameSampling& sampling,
                              rendering::settings::RenderVersion mode) {
    return {{"shader_frame", sampling.shader_frame},
            {"taa_index", sampling.taa_index},
            {"accumulation_index", sampling.accumulation_index},
            {"jitter_pixels", json::array{sampling.jitter_pixels[0], sampling.jitter_pixels[1]}},
            {"random_counter", sampling.random_counter},
            {"random_counter_secondary", sampling.random_counter_secondary},
            {"path_sample_limit", mode == rendering::settings::RenderVersion::path_tracer
                                      ? json::value(sampling.path_sample_limit)
                                      : json::value(nullptr)}};
}

json::object rendering_context(const vulkan::FrameInput& input,
                               const vulkan::FrameSampling& sampling,
                               const vulkan::Statistics& statistics,
                               const std::filesystem::path& settings_path) {
    return {{"mode", render_mode(input.settings->render.version)},
            {"settings_source", source_path(settings_path)},
            {"settings", json::parse(rendering::configuration::serialize(*input.settings))},
            {"output", statistics.direct_output ? "direct_swapchain" : "offscreen_copy"},
            {"presentation", presentation_mode(statistics.presentation_mode)},
            {"chunk_layout",
             statistics.chunk_layout == vulkan::ChunkLayout::packed ? "packed_rg32" : "split_r32"},
            {"sampling", sampling_context(sampling, input.settings->render.version)}};
}

json::object frame_context(const Application& app, const vulkan::FrameInput& input,
                           const vulkan::FrameSampling& sampling,
                           const vulkan::Statistics& statistics) {
    return {{"schema", "engine.frame_context.v1"},
            {"frame", statistics.frame_count},
            {"active", input.active},
            {"sampling_paused", input.paused},
            {"delta_seconds", input.delta_seconds},
            {"voxel_size_m", app.options.voxel_meters},
            {"bounds_budget_groups", input.bounds_budget},
            {"render_extent_px", json::array{statistics.width, statistics.height}},
            {"output_extent_px", json::array{statistics.output_width, statistics.output_height}},
            {"camera", camera_context(*input.camera, app.options.voxel_meters)},
            {"world", world_context(input, app.scene_source)},
            {"rendering", rendering_context(input, sampling, statistics, app.options.settings)},
            {"gpu", device_context(app.renderer->device_info())},
            {"renderer_allocated_bytes", statistics.allocated_bytes},
            {"timing", timing_context(statistics)},
            {"validation_enabled", app.options.validation},
            {"validation_errors", statistics.validation_errors},
            {"build", build_context()},
            {"shaders", shader_package(app.options.shaders, statistics.chunk_layout)}};
}
} // namespace

void copy_frame_context(Application& app, const rendering::vulkan::FrameInput& input,
                        const rendering::vulkan::FrameSampling& sampling) {
    try {
        if (const auto complete = app.renderer->finish(); !complete) {
            throw std::runtime_error(complete.error());
        }
        const auto statistics = app.renderer->statistics();
        const auto text = json::serialize(frame_context(app, input, sampling, statistics));
        if (const auto copied = app.window->set_clipboard(text); !copied) {
            throw std::runtime_error(copied.error());
        }
        app.metrics.status = "Frame context copied";
    } catch (const std::exception& error) {
        app.metrics.status = "Cannot copy frame context: " + std::string(error.what());
    }
}
} // namespace engine::app::sandbox
