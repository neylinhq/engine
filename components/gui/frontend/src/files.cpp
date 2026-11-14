#include "files/path.hpp"
#include "state.hpp"
#include <algorithm>
#include <cstdio>
#include <string_view>
#include <system_error>
namespace engine::gui::frontend {
namespace {
void set_path(FileDialog& dialog, const std::filesystem::path& path) {
    const auto text = path_to_utf8(path);
    std::snprintf(dialog.path.data(), dialog.path.size(), "%s", text.c_str());
}
bool writing(Operation operation) {
    return operation == Operation::save_world || operation == Operation::save_path ||
           operation == Operation::save_settings || operation == Operation::save_materials;
}
std::string_view default_suffix(Operation operation) {
    switch (operation) {
    case Operation::save_world:
        return ".cvox";
    case Operation::save_settings:
        return ".settings.json";
    case Operation::save_materials:
        return ".materials.json";
    default:
        return {};
    }
}
const char* default_name(Operation operation) {
    switch (operation) {
    case Operation::save_settings:
        return "engine.settings.json";
    case Operation::save_materials:
        return "world.materials.json";
    default:
        return writing(operation) ? "world.cvox" : "";
    }
}
const char* file_description(Operation operation) {
    switch (operation) {
    case Operation::import_vox:
        return "MagicaVoxel model (*.vox)";
    case Operation::import_vl32:
        return "Voxel records (*.vl32)";
    case Operation::import_mesh:
        return "Triangle mesh (*.obj, *.stl)";
    case Operation::save_settings:
        return "Engine settings (*.settings.json)";
    case Operation::load_materials:
    case Operation::save_materials:
        return "Material palette (*.materials.json)";
    case Operation::load_path:
    case Operation::save_path:
        return "NAADF camera path (*.pnb)";
    default:
        return "NAADF compressed voxel world (*.cvox)";
    }
}
void fixed_options(UiState& state) {
    ImGui::TextUnformatted("Set World Dimensions:");
    ImGui::SliderInt("Segment size", &state.segment_groups, 1, 8);
    ImGui::SliderInt3("Amount of segments", state.segments.data(), 1, 32);
    const auto scale = state.segment_groups * 64;
    ImGui::Text("Final size: %d, %d, %d", state.segments[0] * scale, state.segments[1] * scale,
                state.segments[2] * scale);
    const auto volume = static_cast<double>(state.segments[0]) * state.segments[1] *
                        state.segments[2] * scale * scale * scale;
    ImGui::Text("Voxel amount: %.2f billion", volume / 1e9);
    if (ImGui::Button("Reset to Default")) {
        state.segment_groups = 4;
        state.segments = {16, 2, 16};
    }
}
void mesh_options(UiState& state) {
    ImGui::SliderInt("Max size", &state.import_options.max_size, 16, 8192);
    ImGui::InputText("Axis Permutation", state.permutation.data(), state.permutation.size());
    ImGui::TextDisabled("Reorder xyz axes; uppercase letters flip an axis.");
    ImGui::SliderInt("Palette size", &state.import_options.palette_size, 4, 4096);
    if (ImGui::Button("Reset to Default")) {
        state.import_options = {};
        state.permutation = {'x', 'z', 'y', '\0'};
    }
}
void select_entry(FileDialog& dialog, const std::filesystem::directory_entry& entry) {
    std::error_code error;
    const auto directory = entry.is_directory(error);
    const auto label = (directory ? "[+] " : "    ") + path_to_utf8(entry.path().filename());
    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
        set_path(dialog, entry.path());
        if (directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            dialog.directory = entry.path();
        }
    }
}
void directory_list(FileDialog& dialog) {
    std::error_code error;
    std::vector<std::filesystem::directory_entry> entries;
    auto iterator = std::filesystem::directory_iterator(dialog.directory, error);
    while (!error && iterator != std::filesystem::directory_iterator{}) {
        entries.push_back(*iterator);
        iterator.increment(error);
    }
    if (error) {
        dialog.error = error.message();
    }
    std::ranges::sort(entries, {}, [](const auto& entry) { return entry.path().filename(); });
    if (ImGui::BeginListBox("Files", {-1, 220})) {
        for (const auto& entry : entries) {
            select_entry(dialog, entry);
        }
        ImGui::EndListBox();
    }
}
void queue_file(UiState& state, const std::filesystem::path& path) {
    const auto scale = state.segment_groups * 64;
    state.import_options.axis_permutation = state.permutation.data();
    state.requests.push_back(
        {*state.file.operation,
         path,
         state.import_options,
         {state.segments[0] * scale, state.segments[1] * scale, state.segments[2] * scale},
         *state.file.operation == Operation::open_fixed ? state.segment_groups : 4});
    state.file.operation.reset();
}
void submit(UiState& state) {
    auto& dialog = state.file;
    auto path = path_from_utf8(dialog.path.data());
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        dialog.directory = path;
        return;
    }
    const auto suffix = default_suffix(*dialog.operation);
    if (!suffix.empty() && !path.empty() && path.extension().empty()) {
        path += suffix;
    }
    const auto readable = std::filesystem::is_regular_file(path, error);
    if (path.empty() || (!writing(*dialog.operation) && !readable)) {
        dialog.error = "Select an existing file or enter a destination file name.";
        if (error) {
            dialog.error = error.message();
        }
        return;
    }
    if (writing(*dialog.operation) && readable) {
        dialog.overwrite_path = path;
        ImGui::OpenPopup("Overwrite File");
        return;
    }
    queue_file(state, path);
    ImGui::CloseCurrentPopup();
}
void footer(UiState& state) {
    if (!state.file.error.empty()) {
        ImGui::TextWrapped("%s", state.file.error.c_str());
    }
    if (ImGui::Button(writing(*state.file.operation) ? "Save" : "Open / Import")) {
        submit(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.file.operation.reset();
        ImGui::CloseCurrentPopup();
    }
}
bool overwrite_dialog(UiState& state) {
    if (!ImGui::BeginPopupModal("Overwrite File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return false;
    }
    ImGui::TextWrapped("This file already exists:\n%s\nReplace it?",
                       path_to_utf8(state.file.overwrite_path).c_str());
    bool confirmed = false;
    if (ImGui::Button("Overwrite")) {
        queue_file(state, state.file.overwrite_path);
        ImGui::CloseCurrentPopup();
        confirmed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return confirmed;
}
void file_contents(UiState& state) {
    auto& dialog = state.file;
    ImGui::TextUnformatted(file_description(*dialog.operation));
    if (*dialog.operation == Operation::open_fixed) {
        fixed_options(state);
    }
    if (*dialog.operation == Operation::import_mesh) {
        mesh_options(state);
    }
    ImGui::TextWrapped("%s", path_to_utf8(dialog.directory).c_str());
    if (ImGui::Button("Parent directory")) {
        dialog.directory = dialog.directory.parent_path();
    }
    directory_list(dialog);
    const auto enter = ImGui::InputText("Path", dialog.path.data(), dialog.path.size(),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    if (enter) {
        submit(state);
    }
    if (dialog.operation) {
        footer(state);
    }
}
} // namespace
void open_file(UiState& state, Operation operation) {
    auto& dialog = state.file;
    dialog.operation = operation;
    dialog.opening = true;
    dialog.error.clear();
    dialog.overwrite_path.clear();
    if (dialog.directory.empty()) {
        dialog.directory = std::filesystem::current_path();
    }
    set_path(dialog, dialog.directory / default_name(operation));
}
void draw_files(UiState& state) {
    auto& dialog = state.file;
    if (dialog.opening) {
        ImGui::OpenPopup("Select File");
        dialog.opening = false;
    }
    ImGui::SetNextWindowSize({720, 500}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Select File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    file_contents(state);
    if (overwrite_dialog(state)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
} // namespace engine::gui::frontend
