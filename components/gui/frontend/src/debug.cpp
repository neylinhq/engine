#include "state.hpp"
namespace engine::gui::frontend {
namespace {
void frame_metrics(const UiState& state, const Metrics& metrics) {
    const auto time = metrics.milliseconds;
    ImGui::Text("FPS: %.1f", time > 0 ? 1000 / time : 0);
    ImGui::Text("Frametime (ms): %.3f", time);
    ImGui::Text("Render: %u x %u | GPU: %.3f ms", metrics.render_width, metrics.render_height,
                metrics.gpu_milliseconds);
    ImGui::Text("Chunk storage: %s", metrics.chunk_storage.c_str());
    ImGui::Text("Output: %s", metrics.output_path.c_str());
    ImGui::TextWrapped("%s", metrics.status.c_str());
    if (!state.error.empty()) {
        ImGui::TextWrapped("%s", state.error.c_str());
    }
}
void world_metrics(UiState& state, const Context& context) {
    const auto size = context.world.size();
    ImGui::Text("Size: [%d, %d, %d]", size.x, size.y, size.z);
    ImGui::Text("%.3f Billion Voxels", static_cast<double>(size.x) * size.y * size.z / 1e9);
    ImGui::SliderInt("AADF speedup", &state.bounds_speedup, 0, 512);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How fast the AADFs should be computed in the background");
    }
    const auto view = context.world.view();
    ImGui::Text("Chunks memory usage: %.3f MB", static_cast<double>(view.chunks.size()) * 8e-6);
    ImGui::Text("Blocks memory usage: %.3f MB", static_cast<double>(view.blocks.size()) * 4e-6);
    ImGui::Text("Voxels memory usage: %.3f MB", static_cast<double>(view.voxels.size()) * 4e-6);
    ImGui::Text("Voxels compression factor: %.3f", context.world.compression_factor());
    ImGui::Text("GPU memory allocated: %.3f MB",
                static_cast<double>(context.metrics.gpu_bytes) * 1e-6);
    ImGui::Text("Chunks: %zu | Blocks: %zu | Words: %zu", view.chunks.size(), view.blocks.size(),
                view.voxels.size());
}
} // namespace
void draw_debug(UiState& state, Context& context) {
    if (!state.panels.debug) {
        return;
    }
    ImGui::SetNextWindowSize({400, 250}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({5, 23}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", &state.panels.debug)) {
        frame_metrics(state, context.metrics);
        if (ImGui::Button("Copy Frame Context")) {
            enqueue(state, Operation::copy_frame_context);
        }
        ImGui::Separator();
        world_metrics(state, context);
        if (!context.walking) {
            ImGui::Text("Flight boost: %.2f", context.camera.boost());
        }
        const auto position = context.camera.pose().position;
        ImGui::Text("Camera: %.2f %.2f %.2f", position.x, position.y, position.z);
    }
    ImGui::End();
}
} // namespace engine::gui::frontend
