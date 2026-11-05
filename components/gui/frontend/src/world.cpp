#include "state.hpp"
#include <exception>
namespace engine::gui::frontend {
namespace {
void model_item(UiState& state, Context& context, std::size_t index) {
    const auto& model = context.world.models()[index];
    ImGui::PushID(static_cast<int>(index));
    if (ImGui::Selectable(model.name.c_str(), state.editing.model == static_cast<int>(index))) {
        state.editing.model = static_cast<int>(index);
    }
    if (ImGui::BeginPopupContextItem("ModelContext")) {
        if (ImGui::MenuItem("Create Entity")) {
            try {
                static_cast<void>(context.world.create_entity_type(index));
            } catch (const std::exception& error) {
                state.error = error.what();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
void models(UiState& state, Context& context) {
    if (ImGui::Button("Import Model")) {
        open_file(state, Operation::import_model);
    }
    if (ImGui::BeginListBox("Models")) {
        for (std::size_t index = 0; index < context.world.models().size(); ++index) {
            model_item(state, context, index);
        }
        ImGui::EndListBox();
    }
    const auto index = state.editing.model;
    if (index >= 0 && static_cast<std::size_t>(index) < context.world.models().size()) {
        const auto size = context.world.models()[static_cast<std::size_t>(index)].size;
        ImGui::Text("Size: %d | %d | %d", size.x, size.y, size.z);
    } else {
        ImGui::TextUnformatted("Size: <Select a model to show>");
    }
    ImGui::TextWrapped("Right click a model to create an entity type.");
}
void start_spawn(UiState& state, Context& context, std::size_t index) {
    auto& editing = state.editing;
    editing.spawn_begin = context.world.entities().size();
    editing.spawn_type = index;
    const auto count = editing.spawn_amount[0] * editing.spawn_amount[1] * editing.spawn_amount[2];
    try {
        for (int instance = 0; instance < count; ++instance) {
            static_cast<void>(context.world.spawn_entity(index, context.camera.pose().position));
        }
        editing.spawning = true;
    } catch (const std::exception& error) {
        context.world.remove_entities_from(editing.spawn_begin);
        state.error = error.what();
    }
}
void entity_item(UiState& state, Context& context, std::size_t index) {
    ImGui::PushID(static_cast<int>(index));
    const auto& type = context.world.entity_types()[index];
    if (ImGui::Selectable(type.name.c_str(),
                          state.editing.entity_type == static_cast<int>(index))) {
        state.editing.entity_type = static_cast<int>(index);
    }
    if (ImGui::BeginPopupContextItem("EntityContext")) {
        if (ImGui::MenuItem("Spawn")) {
            start_spawn(state, context, index);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
void entities(UiState& state, Context& context) {
    if (state.editing.spawning) {
        ImGui::TextWrapped("Place entities by left clicking or Esc to cancel!");
        return;
    }
    if (ImGui::BeginListBox("Entity types")) {
        for (std::size_t index = 0; index < context.world.entity_types().size(); ++index) {
            entity_item(state, context, index);
        }
        ImGui::EndListBox();
    }
    ImGui::SliderFloat("Rotation speed", &state.editing.rotation_speed, 0, 100, "%.2f",
                       ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Clear all")) {
        context.world.clear_entities();
    }
    ImGui::SliderInt3("Amount to spawn", state.editing.spawn_amount.data(), 1, 25);
    const auto selected = state.editing.entity_type;
    if (selected >= 0 && static_cast<std::size_t>(selected) < context.world.entity_types().size()) {
        const auto size = context.world.entity_types()[static_cast<std::size_t>(selected)].size;
        ImGui::Text("Size: %d | %d | %d", size.x, size.y, size.z);
    }
    ImGui::Text("Instances: %zu", context.world.entities().size());
    ImGui::TextWrapped("Create entity types in the Models tab. Right click a type to spawn a grid. "
                       "The maximum entity size is 128 cubed voxels.");
}
void model_selector(Editing& editing, Context& context) {
    ImGui::Checkbox("Place at camera", &editing.place_camera);
    const auto models = context.world.models();
    const auto valid =
        editing.stamp_model >= 0 && static_cast<std::size_t>(editing.stamp_model) < models.size();
    const auto name =
        valid ? models[static_cast<std::size_t>(editing.stamp_model)].name.c_str() : "Select";
    if (!ImGui::BeginCombo("Model", name)) {
        return;
    }
    for (std::size_t index = 0; index < models.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(models[index].name.c_str(),
                              editing.stamp_model == static_cast<int>(index))) {
            editing.stamp_model = static_cast<int>(index);
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
}
void tool_options(Editing& editing, Context& context) {
    using world::editing::Tool;
    auto tool = static_cast<int>(editing.tool);
    if (ImGui::Combo("Tool", &tool, "None\0Sphere\0Cube\0Paint\0FloodFill\0Model\0")) {
        editing.erase = false;
        editing.continuous = true;
        editing.radius = 10;
        editing.place_camera = false;
        editing.stamp_model = -1;
    }
    editing.tool = static_cast<Tool>(tool);
    if (editing.tool == Tool::sphere || editing.tool == Tool::cube) {
        ImGui::Checkbox("Erase", &editing.erase);
        ImGui::Checkbox("Continuous", &editing.continuous);
    }
    if (editing.tool == Tool::sphere || editing.tool == Tool::cube || editing.tool == Tool::paint) {
        ImGui::SliderFloat("Radius", &editing.radius, 1, 400, "%.2f", ImGuiSliderFlags_Logarithmic);
    }
    if (editing.tool == Tool::model) {
        model_selector(editing, context);
    }
}
void editing_tab(UiState& state, Context& context) {
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_material(state, context);
    }
    if (ImGui::CollapsingHeader("Editing tool", ImGuiTreeNodeFlags_DefaultOpen)) {
        tool_options(state.editing, context);
    }
}
void debug_tab(const Context& context) {
    ImGui::Text("Entity Cpu - Gpu Copy: %.2f KB", context.metrics.entity_kilobytes);
    ImGui::Text("Processing time entities: %.2f ms", context.metrics.entity_milliseconds);
    ImGui::Text("Processing time data change: %.2f ms", context.metrics.editing_milliseconds);
}
void world_tabs(UiState& state, Context& context) {
    if (!ImGui::BeginTabBar("TabsEditing")) {
        return;
    }
    if (ImGui::BeginTabItem("Models")) {
        models(state, context);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Entities")) {
        entities(state, context);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Editing")) {
        editing_tab(state, context);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Debug")) {
        debug_tab(context);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
} // namespace
void draw_world(UiState& state, Context& context) {
    if (!state.panels.world) {
        return;
    }
    ImGui::SetNextWindowSize({300, 400}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 305, 23}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World", &state.panels.world)) {
        world_tabs(state, context);
    }
    ImGui::End();
}
} // namespace engine::gui::frontend
