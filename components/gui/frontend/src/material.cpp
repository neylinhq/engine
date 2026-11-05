#include "state.hpp"
#include <cstdio>
#include <exception>
namespace engine::gui::frontend {
namespace {
void material_list(UiState& state, Context& context) {
    if (!ImGui::BeginListBox("Materials")) {
        return;
    }
    const auto materials = context.world.materials();
    for (std::size_t index = 1; index < materials.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(materials[index].name.c_str(), state.editing.material == index)) {
            state.editing.material = static_cast<std::uint16_t>(index);
        }
        if (state.editing.focus_material && state.editing.material == index) {
            ImGui::SetScrollHereY();
            state.editing.focus_material = false;
        }
        ImGui::PopID();
    }
    ImGui::EndListBox();
}
bool optical_fields(world::model::Material& material) {
    using world::model::MaterialBase;
    if (material.base == MaterialBase::diffuse) {
        return color_vector("Albedo", material.color_base);
    }
    if (material.base == MaterialBase::emissive) {
        auto changed = color_vector("Albedo", material.color_base);
        auto strength = static_cast<float>(material.color_layer.x);
        changed |= ImGui::SliderFloat("Emissive strength", &strength, 0.02F, 600, "%.2f",
                                      ImGuiSliderFlags_Logarithmic);
        material.color_layer.x = strength;
        return changed;
    }
    float index[]{static_cast<float>(material.color_base.x),
                  static_cast<float>(material.color_base.y),
                  static_cast<float>(material.color_base.z)};
    auto changed =
        ImGui::SliderFloat3("Refractive index", index, 1, 10, "%.2f", ImGuiSliderFlags_Logarithmic);
    material.color_base = {index[0], index[1], index[2]};
    if (material.base == MaterialBase::metallic_rough) {
        changed |= ImGui::SliderFloat("Roughness", &material.roughness, 0.001F, 1, "%.5f",
                                      ImGuiSliderFlags_Logarithmic);
    }
    return changed;
}
bool base_selector(world::model::Material& material) {
    constexpr std::array<const char*, 4> names{"Diffuse", "Emissive", "MetallicRough",
                                               "MetallicMirror"};
    const auto old = static_cast<std::size_t>(material.base);
    if (!ImGui::BeginCombo("Material Base", old < names.size() ? names[old] : "Unknown")) {
        return false;
    }
    bool changed = false;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index == 3 && material.layer != world::model::MaterialLayer::none) {
            continue;
        }
        if (ImGui::Selectable(names[index], index == old)) {
            changed = true;
            material.base = static_cast<world::model::MaterialBase>(index);
            if (index == 2) {
                material.roughness = 0.05F;
            }
        }
        if (index == old) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
    return changed;
}
void material_fields(UiState& state, Context& context) {
    const auto index = state.editing.material;
    if (index >= context.world.materials().size() || index == 0) {
        return;
    }
    auto material = context.world.materials()[index];
    std::array<char, 256> name{};
    std::snprintf(name.data(), name.size(), "%s", material.name.c_str());
    auto changed =
        ImGui::InputText("name", name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    material.name = name.data();
    changed |= base_selector(material);
    changed |= optical_fields(material);
    if (changed) {
        try {
            context.world.set_material(index, std::move(material));
        } catch (const std::exception& error) {
            state.error = error.what();
        }
    }
}
} // namespace
void draw_material(UiState& state, Context& context) {
    if (ImGui::Button("Create new type")) {
        world::model::Material material;
        try {
            state.editing.material = context.world.add_material(std::move(material));
            state.editing.focus_material = true;
        } catch (const std::exception& error) {
            state.error = error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Select Material")) {
        state.editing.pick_material = true;
    }
    material_list(state, context);
    if (state.editing.pick_material) {
        ImGui::TextWrapped("Select a voxel for material selection or press Esc to cancel.");
    }
    material_fields(state, context);
}
} // namespace engine::gui::frontend
