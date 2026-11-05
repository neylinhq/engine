#include "session.hpp"
#include <algorithm>
#include <cmath>
namespace engine::app::benchmark {
namespace {
void validate_window(Session& session) {
    session.window->poll();
    const auto extent = session.window->framebuffer();
    if (session.window->closed() || session.window->minimized() ||
        extent.width != session.scenario.width || extent.height != session.scenario.height) {
        throw std::runtime_error("Benchmark surface was closed, minimized or resized");
    }
    if (!take(session.renderer->begin_frame())) {
        throw std::runtime_error("Benchmark drawable became unavailable");
    }
}
rendering::vulkan::FrameInput input(Session& session) {
    rendering::vulkan::FrameInput result;
    result.world = session.world.view();
    result.entities = &session.world.entity_gpu();
    result.camera = &*session.camera;
    result.settings = &session.scenario.settings;
    result.delta_seconds = session.scenario.step_seconds;
    result.bounds_budget = session.scenario.bounds_budget;
    return result;
}
void frame(Session& session, FrameSample& sample, bool scripted) {
    const auto start = Clock::now();
    validate_window(session);
    session.camera->clear_changed();
    sample.camera = scripted ? camera_at(session.scenario.camera, sample.frame)
                             : session.scenario.camera.front().pose;
    auto pose = sample.camera;
    pose.position = pose.position / session.scenario.voxel_size;
    take(session.camera->set_pose(pose));
    if (scripted) {
        apply_events(session, sample);
    }
    session.world.update_entities(
        session.scenario.step_seconds, 0,
        session.renderer->history_index(session.scenario.settings.render.version));
    take(session.renderer->draw(input(session)));
    session.world.acknowledge_changes();
    sample.wall_ms = elapsed(start);
}
void record(FrameSample& sample, const rendering::vulkan::Statistics& statistics) {
    sample.submission = statistics.frame_count;
    sample.wait_ms = statistics.cpu_wait_milliseconds;
    sample.record_ms = statistics.cpu_record_milliseconds;
    sample.submit_ms = statistics.cpu_submit_milliseconds;
    sample.renderer_bytes = statistics.allocated_bytes;
    if (statistics.validation_errors != 0) {
        throw std::runtime_error("Vulkan validation reported errors");
    }
}
void capture(Session& session) {
    validate_window(session);
    auto captured = input(session);
    captured.capture_output = true;
    take(session.renderer->draw(captured));
    take(session.renderer->screenshot(session.options.output / "final.png"));
    take(session.renderer->finish());
    if (session.renderer->statistics().validation_errors != 0) {
        throw std::runtime_error("Vulkan validation failed during final capture");
    }
}
} // namespace
void collect_gpu(Measurements& result, const rendering::vulkan::Statistics& statistics) {
    if (statistics.gpu_frame_count <= result.first_submission) {
        return;
    }
    const auto index = statistics.gpu_frame_count - result.first_submission - 1;
    if (index >= result.frames.size()) {
        throw std::runtime_error("GPU timestamp submission is outside the measured range");
    }
    auto& sample = result.frames[static_cast<std::size_t>(index)];
    if (sample.submission != statistics.gpu_frame_count ||
        !std::isfinite(statistics.gpu_milliseconds) || statistics.gpu_milliseconds < 0) {
        throw std::runtime_error("GPU timestamp does not match its submitted frame");
    }
    sample.gpu_ms = statistics.gpu_milliseconds;
    if (statistics.world_updates_profiled) {
        sample.gpu_prepare_ms = statistics.gpu_prepare_milliseconds;
        sample.gpu_render_ms = statistics.gpu_render_milliseconds;
    }
    sample.passes = statistics.gpu_passes;
}
void execute(Session& session) {
    auto& results = session.results;
    const auto warmup = Clock::now();
    for (unsigned index = 0; index < session.scenario.warmup_frames; ++index) {
        FrameSample sample;
        frame(session, sample, false);
    }
    take(session.renderer->finish());
    results.warmup_ms = elapsed(warmup);
    results.first_submission = session.renderer->statistics().frame_count;
    observe_memory(session, "warmed");
    const auto start = Clock::now();
    for (unsigned index = 0; index < session.scenario.frames; ++index) {
        auto& sample = results.frames[index];
        sample.frame = index;
        frame(session, sample, true);
        const auto observation = Clock::now();
        const auto status = session.renderer->statistics();
        record(sample, status);
        sample.revision = session.world.view().revision;
        collect_gpu(results, status);
        if ((index + 1) % session.scenario.memory_interval == 0) {
            observe_memory(session, "measure", index);
        }
        results.observation_ms += elapsed(observation);
    }
    const auto drain = Clock::now();
    take(session.renderer->finish());
    results.final_drain_ms = elapsed(drain);
    results.measurement_ms = elapsed(start);
    collect_gpu(results, session.renderer->statistics());
    if (std::ranges::any_of(results.frames, [](const auto& sample) { return !sample.gpu_ms; })) {
        throw std::runtime_error("Not all measured GPU timestamps were collected");
    }
    observe_memory(session, "finished", session.scenario.frames - 1);
    if (session.options.capture) {
        capture(session);
    }
}
} // namespace engine::app::benchmark
