#include "state.hpp"
namespace engine::gui::frontend {
namespace {
void file_menu(UiState& state) {
    if (ImGui::BeginMenu("Open")) {
        if (ImGui::MenuItem("Fit to Model")) {
            open_file(state, Operation::open_world);
        }
        if (ImGui::MenuItem("Fixed size")) {
            open_file(state, Operation::open_fixed);
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Save As")) {
        open_file(state, Operation::save_world);
    }
    if (ImGui::MenuItem("Import Vox")) {
        open_file(state, Operation::import_vox);
    }
    if (ImGui::MenuItem("Import VL32")) {
        open_file(state, Operation::import_vl32);
    }
    if (ImGui::MenuItem("Import Mesh")) {
        open_file(state, Operation::import_mesh);
    }
}
} // namespace
void draw_menu(UiState& state, bool walking) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        file_menu(state);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Settings", nullptr, &state.panels.settings);
        ImGui::MenuItem("Debug", nullptr, &state.panels.debug);
        ImGui::MenuItem("Sky Debug", nullptr, &state.panels.sky);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        ImGui::MenuItem("World", nullptr, &state.panels.world);
        ImGui::MenuItem("Path Builder", nullptr, &state.panels.path);
        ImGui::EndMenu();
    }
    const auto* controls = walking ? "F: fly   Shift: run   Space: jump"
                                   : "F: walk   Shift: boost   Space / Ctrl: ascend / descend";
    ImGui::TextDisabled("F1 / Tab: UI   %s   F11: borderless", controls);
    ImGui::EndMainMenuBar();
}
} // namespace engine::gui::frontend
