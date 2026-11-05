#include "files/path.hpp"
#include "state.hpp"
#include <cmath>
#include <cstdio>
namespace engine::gui::frontend {
namespace {
motion::path::Node current_node(const UiState& state, const Context& context) {
    auto name = std::string(state.node_name.data());
    if (name.empty()) {
        name = "Node " + std::to_string(context.path.nodes().size() + 1);
    }
    return {context.camera.pose().position, context.camera.basis().forward, 1, std::move(name)};
}
void apply_node(UiState& state, Context& context) {
    const auto& node = context.path.nodes()[static_cast<std::size_t>(state.selected_node)];
    motion::camera::Pose pose{
        node.position, std::atan2(node.direction.x, node.direction.z),
        std::atan2(node.direction.y, std::hypot(node.direction.x, node.direction.z))};
    if (const auto result = context.camera.set_pose(pose); !result) {
        state.error = result.error();
    }
}
void insert_node(UiState& state, Context& context, std::size_t index) {
    if (const auto result = context.path.insert(index, current_node(state, context)); !result) {
        state.error = result.error();
    }
    state.node_name[0] = '\0';
}
void selected_actions(UiState& state, Context& context) {
    if (state.selected_node < 0) {
        return;
    }
    const auto selected = static_cast<std::size_t>(state.selected_node);
    if (ImGui::Button("Insert Above")) {
        insert_node(state, context, selected);
    }
    ImGui::SameLine();
    if (ImGui::Button("Insert Below")) {
        insert_node(state, context, selected + 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        apply_node(state, context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        context.path.erase(selected);
        state.selected_node = -1;
    }
}
void node_list(UiState& state, Context& context) {
    const auto nodes = context.path.nodes();
    if (state.selected_node >= static_cast<int>(nodes.size())) {
        state.selected_node = -1;
    }
    if (!ImGui::BeginListBox("Nodes", {300, 300})) {
        return;
    }
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(nodes[index].name.c_str(),
                              state.selected_node == static_cast<int>(index))) {
            state.selected_node = static_cast<int>(index);
        }
        ImGui::PopID();
    }
    ImGui::EndListBox();
}
void edit_node(UiState& state, Context& context) {
    if (state.selected_node < 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(state.selected_node);
    auto node = context.path.nodes()[index];
    auto changed = edit_vector("Position", node.position);
    changed |= edit_vector("Direction", node.direction);
    auto duration = static_cast<float>(node.duration);
    changed |= ImGui::SliderFloat("Time", &duration, 0.2F, 20, "%.2f");
    std::array<char, 256> name{};
    std::snprintf(name.data(), name.size(), "%s", node.name.c_str());
    changed |= ImGui::InputText("Name", name.data(), name.size());
    if (changed) {
        node.name = name.data();
        node.duration = duration;
        if (const auto result = context.path.set(index, std::move(node)); !result) {
            state.error = result.error();
        }
    }
}
void playback(UiState& state, Context& context) {
    ImGui::Text("Average ms: %.3f", context.path.average_milliseconds());
    ImGui::Checkbox("Screenshot at end", &state.screenshot_at_end);
    if (ImGui::Button("Screenshot now")) {
        enqueue(state, Operation::screenshot);
    }
    if (ImGui::Button(context.path.playing() ? "Stop" : "Play")) {
        if (context.path.playing()) {
            context.path.stop();
        } else {
            context.path.start();
        }
    }
    ImGui::SameLine();
    ImGui::Text("Total time: %.2fs", context.path.duration());
}
void path_file(UiState& state) {
    ImGui::InputText("File Name", state.path_name.data(), state.path_name.size());
    auto path = path_from_utf8(state.path_name.data());
    if (path.empty()) {
        ImGui::TextDisabled("Enter a file name to save or load a .pnb path.");
        return;
    }
    if (path.extension().empty()) {
        path += ".pnb";
    }
    if (ImGui::Button("Save")) {
        Request request;
        request.operation = Operation::save_path;
        request.path = path;
        state.requests.push_back(std::move(request));
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        Request request;
        request.operation = Operation::load_path;
        request.path = path;
        state.requests.push_back(std::move(request));
    }
}
} // namespace
void draw_path(UiState& state, Context& context) {
    if (!state.panels.path) {
        return;
    }
    if (ImGui::Begin("Path Editor", &state.panels.path)) {
        playback(state, context);
        node_list(state, context);
        selected_actions(state, context);
        if (ImGui::Button("Add Node At End")) {
            insert_node(state, context, context.path.nodes().size());
        }
        ImGui::InputText("Node Name", state.node_name.data(), state.node_name.size());
        ImGui::Separator();
        edit_node(state, context);
        ImGui::Separator();
        path_file(state);
    }
    ImGui::End();
}
} // namespace engine::gui::frontend
