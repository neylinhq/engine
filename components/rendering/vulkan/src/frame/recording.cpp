#include "../state.hpp"

#include <backends/imgui_impl_vulkan.h>
#include <chrono>
#include <imgui.h>
#include <stdexcept>

namespace engine::rendering::vulkan {

void Renderer::Impl::record_base(const FrameInput& input, const UniformValues& values) {
    const auto pixels = (render_extent.width * render_extent.height + 63u) / 64u;
    const auto buckets = ((render_extent.width + 7u) / 8u) * ((render_extent.height + 7u) / 8u);
    dispatch("base_renderAtmosphere_precomputeAtmosphere", 4096, values);
    dispatch("base_renderFirstHit_calcFirstHit", pixels, values);
    dispatch("base_renderTaaSampleReverse_reprojectOldSamples", pixels, values);
    dispatch("base_renderSampleRefine_clearBucketsAndCalcMask", (buckets + 63u) / 64u, values);
    dispatch("base_rayQueueCalc_calcRayQueue", pixels, values);
    dispatch("base_rayQueueCalc_calcRayQueueStore", 1, values);
    dispatch_indirect("base_renderGlobalIllum_calcGlobalIlum", "groupCount", values);
    dispatch("base_renderSampleRefine_computeValidHistory", 1, values);
    dispatch_indirect("base_renderSampleRefine_countValidDataAndRefine", "globalIlumValidDispatch",
                      values);
    dispatch_indirect("base_renderSampleRefine_countInvalidData", "globalIlumInvalidDispatch",
                      values);
    dispatch("base_renderSampleRefine_refineBuckets", (buckets + 63u) / 64u, values);
    dispatch("base_renderSpatialResampling_calcSpatialResampling", pixels, values);
    if (input.settings->render.base.denoise) {
        dispatch("base_renderDenoiseSplit_calcDenoiseHorizontal", pixels, values);
        dispatch("base_renderDenoiseSplit_calcDenoiseVertical", pixels, values);
    }
    dispatch("base_renderTaaSampleReverse_calcNewTaaSample", pixels, values);
}

void Renderer::Impl::record_albedo(const FrameInput& input, const UniformValues& values) {
    const auto pixels = (render_extent.width * render_extent.height + 63u) / 64u;
    dispatch("albedo_renderFirstHit_calcFirstHit", pixels, values);
    if (input.settings->render.albedo.taa_sample_max_age > 1 &&
        !input.settings->render.show_steps) {
        dispatch("albedo_renderTaaSampleReverse_reprojectOldSamples", pixels, values);
    }
}

void Renderer::Impl::record_path_tracer(const UniformValues& values) {
    const auto pixels = (render_extent.width * render_extent.height + 63u) / 64u;
    dispatch("pathTracer_renderAtmosphere_precomputeAtmosphere", 4096, values);
    dispatch("pathTracer_renderFirstHit_calcFirstHit", pixels, values);
    dispatch("pathTracer_renderGlobalIllum_calcGlobalIlum", pixels, values);
}

void Renderer::Impl::record_original(const FrameInput& input, const UniformValues& values) {
    if (mode == settings::RenderVersion::base) {
        record_base(input, values);
    } else if (mode == settings::RenderVersion::albedo) {
        record_albedo(input, values);
    } else {
        record_path_tracer(values);
    }
}

void Renderer::Impl::record_final(const UniformValues& values, const FrameInput& input) {
    prepare_output(input);
    const auto prefix = mode == settings::RenderVersion::base     ? "base"
                        : mode == settings::RenderVersion::albedo ? "albedo"
                                                                  : "pathTracer";
    auto& selected = program(std::string(prefix) + "_renderFinal_MainPS");
    write_uniforms(selected, values);
    update_descriptors(selected);
    begin_output(vk::AttachmentLoadOp::eClear);
    command.bindPipeline(vk::PipelineBindPoint::eGraphics, *selected.pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *selected.pipeline_layout, 0,
                               selected.descriptors, {});
    command.setViewport(
        0, vk::Viewport(0, 0, float(surface_extent.width), float(surface_extent.height), 0, 1));
    command.setScissor(0, vk::Rect2D({}, surface_extent));
    command.draw(3, 1, 0, 0);
    command.endRendering();
    if (input.capture_scene) {
        copy_scene();
    }
    if (ImGui::GetDrawData()->TotalVtxCount != 0) {
        memory_barrier(command);
        begin_output(vk::AttachmentLoadOp::eLoad);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
        command.endRendering();
    }
    ++status.passes;
    present_output();
    profile_pass(selected.name);
}

void Renderer::Impl::draw(const FrameInput& input) {
    if (!acquired || !input.camera || !input.settings) {
        throw std::runtime_error("Renderer draw requires an acquired frame, camera and settings");
    }
    ImGui::SetCurrentContext(context);
    ImGui::Render();
    const auto wait_start = std::chrono::steady_clock::now();
    if (configuration.defer_frame_wait) {
        complete_frame();
    }
    const auto record_start = std::chrono::steady_clock::now();
    if (mode != input.settings->render.version || restart_requested) {
        serial = 0;
    }
    if (input.active && !input.paused) {
        ++serial;
    }
    command.reset();
    command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    begin_timing();
    status.passes = 0;
    status.entity_upload_bytes = 0;
    scene_ready = false;
    prepare_resources(input);
    prepared_timestamp();
    update_history(input);
    const auto& values = parameters(input);
    record_original(input, values);
    record_final(values, input);
    end_timing();
    command.end();
    const auto submit_start = std::chrono::steady_clock::now();
    submit();
    const auto submit_end = std::chrono::steady_clock::now();
    using Milliseconds = std::chrono::duration<double, std::milli>;
    status.cpu_wait_milliseconds = Milliseconds(record_start - wait_start).count();
    status.cpu_record_milliseconds = Milliseconds(submit_start - record_start).count();
    status.cpu_submit_milliseconds = Milliseconds(submit_end - submit_start).count();
    force_path_reset = false;
    restart_requested = false;
}

} // namespace engine::rendering::vulkan
