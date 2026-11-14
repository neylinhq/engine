#include "state.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine::world::model {
namespace {

constexpr std::size_t kMaterialLimit = 32768;

bool named_material(std::string_view name) {
    return !name.empty() && !name.starts_with('_');
}

bool has_material_zero(std::span<const std::uint32_t> nodes) {
    return std::ranges::find(nodes, 0x40000000U) != nodes.end();
}

bool has_reserved_zero(const Model& model) {
    // Legacy CVox can use local ID zero on solid nodes; occupancy is stored separately.
    return !has_material_zero(model.chunks) && !has_material_zero(model.blocks) &&
           std::ranges::none_of(model.voxels, [](std::uint32_t word) {
               return (word & 0xFFFFU) == 0x8000U || (word >> 16) == 0x8000U;
           });
}

using MaterialKeys = std::unordered_map<std::string, std::uint16_t>;

MaterialKeys material_keys(const MaterialLayout& layout) {
    MaterialKeys keys;
    for (std::size_t id = 1; id < layout.slots.size(); ++id) {
        const auto& slot = layout.slots[id];
        if (slot.scope != MaterialScope::vacant && !slot.key.empty()) {
            keys.emplace(slot.key, static_cast<std::uint16_t>(id));
        }
    }
    return keys;
}

std::unordered_set<std::string_view> repeated_names(std::span<const Material> materials) {
    std::unordered_set<std::string_view> names;
    std::unordered_set<std::string_view> repeated;
    for (const auto& material : materials) {
        if (named_material(material.name) && !names.insert(material.name).second) {
            repeated.insert(material.name);
        }
    }
    return repeated;
}

class MaterialAllocation {
  public:
    MaterialAllocation(MaterialLayout& layout, std::vector<Material>& materials)
        : layout_(layout), materials_(materials) {}

    std::uint16_t add(Material material, MaterialScope scope, std::string key = {}) {
        auto& slots = layout_.slots;
        while (next_ < slots.size() && slots[next_].scope != MaterialScope::vacant) {
            ++next_;
        }
        if (next_ >= kMaterialLimit) {
            throw std::runtime_error("NAADF material limit reached");
        }
        MaterialSlot slot{scope, std::move(key)};
        if (next_ == slots.size()) {
            slots.push_back(std::move(slot));
            materials_.push_back(std::move(material));
        } else {
            slots[next_] = std::move(slot);
            materials_[next_] = std::move(material);
        }
        return static_cast<std::uint16_t>(next_++);
    }

  private:
    MaterialLayout& layout_;
    std::vector<Material>& materials_;
    std::size_t next_ = 1;
};

std::vector<std::uint16_t> bind_palette(const Model& source, MaterialPalette palette,
                                        MaterialScope scope, MaterialLayout& layout,
                                        std::vector<Material>& materials) {
    const auto embedded = std::span(source.materials);
    if ((!palette.values.empty() && palette.values.size() != embedded.size()) ||
        (!palette.keys.empty() && palette.keys.size() != embedded.size())) {
        throw std::invalid_argument("Material properties and keys must match the source palette");
    }
    const auto effective = palette.values.empty() ? embedded : palette.values;
    const bool explicit_keys = !palette.keys.empty();
    const auto first = !explicit_keys && has_reserved_zero(source) ? 1U : 0U;
    const auto repeated = explicit_keys ? std::unordered_set<std::string_view>{}
                                        : repeated_names(embedded.subspan(first));
    auto keys = material_keys(layout);
    MaterialAllocation allocation(layout, materials);
    std::vector<std::uint16_t> ids(embedded.size());
    for (std::size_t local = first; local < embedded.size(); ++local) {
        const auto name =
            explicit_keys ? palette.keys[local] : std::string_view(embedded[local].name);
        auto key = (explicit_keys || named_material(name)) && !repeated.contains(name)
                       ? std::string(name)
                       : std::string{};
        const auto found = keys.find(key);
        if (found != keys.end()) {
            const auto id = found->second;
            auto& slot = layout.slots[id];
            const auto existing_scope = slot.scope;
            if ((scope == MaterialScope::scene && existing_scope == MaterialScope::persistent) ||
                (scope == MaterialScope::persistent && existing_scope == MaterialScope::scene)) {
                materials[id] = effective[local];
                slot.scope = MaterialScope::shared;
            }
            ids[local] = id;
            continue;
        }
        const auto id = allocation.add(effective[local], scope, std::move(key));
        if (!layout.slots[id].key.empty()) {
            keys.emplace(layout.slots[id].key, id);
        }
        ids[local] = id;
    }
    return ids;
}

std::vector<std::array<std::uint32_t, 4>> pack_materials(std::span<const Material> materials) {
    std::vector<std::array<std::uint32_t, 4>> packed;
    packed.reserve(materials.size());
    std::ranges::transform(materials, std::back_inserter(packed), pack_material);
    return packed;
}

} // namespace

void remap_materials(Model& model, std::span<const std::uint16_t> mapping) {
    for (auto& word : model.chunks) {
        if ((word >> 30) == 1) {
            word = 0x40000000U | mapping[word & 0x3FFFFFFFU];
        }
    }
    for (auto& word : model.blocks) {
        if ((word >> 30) == 1) {
            word = 0x40000000U | mapping[word & 0x3FFFFFFFU];
        }
    }
    for (auto& word : model.voxels) {
        const auto low = word & 0xFFFFU;
        const auto high = word >> 16;
        const auto first = (low & 0x8000U) != 0 ? 0x8000U | mapping[low & 0x7FFFU] : low;
        const auto second = (high & 0x8000U) != 0 ? 0x8000U | mapping[high & 0x7FFFU] : high;
        word = first | (second << 16);
    }
}

MaterialLayout World::State::scene_layout() const {
    auto layout = *model.material_layout;
    layout.base_revision = material_revision;
    for (auto& slot : layout.slots) {
        if (is_persistent(slot.scope)) {
            slot.scope = MaterialScope::persistent;
        } else {
            slot = {};
        }
    }
    while (layout.slots.back().scope == MaterialScope::vacant) {
        layout.slots.pop_back();
    }
    return layout;
}

void World::State::update_first_vacant_material() {
    const auto& slots = model.material_layout->slots;
    const auto first = std::ranges::find(slots, MaterialScope::vacant, &MaterialSlot::scope);
    first_vacant_material = static_cast<std::size_t>(first - slots.begin());
}

std::vector<Material> World::State::effective_palette(std::vector<Material> baseline) const {
    const auto& slots = model.material_layout->slots;
    for (std::size_t id = 1; id < slots.size(); ++id) {
        const auto scope = slots[id].scope;
        if (scope == MaterialScope::scene || scope == MaterialScope::shared) {
            baseline[id] = model.materials[id];
        }
    }
    return baseline;
}

void World::State::publish_materials(std::vector<Material> updated) {
    auto packed = pack_materials(updated);
    model.materials = std::move(updated);
    packed_materials = std::move(packed);
    ++revision;
    ++material_revision;
}

void World::State::validate_prepared(const Model& prepared) const {
    if (prepared.material_context != material_context) {
        throw std::invalid_argument("Generated world belongs to another material registry");
    }
    if (!prepared.material_layout) {
        throw std::invalid_argument("Scene is not prepared for this world");
    }
    const auto& incoming = *prepared.material_layout;
    if (incoming.base_revision != material_revision) {
        throw std::invalid_argument("World materials changed after scene preparation");
    }
    const auto& current = model.material_layout->slots;
    for (std::size_t id = 0; id < std::max(current.size(), incoming.slots.size()); ++id) {
        const auto was_persistent = id < current.size() && is_persistent(current[id].scope);
        const auto is_retained =
            id < incoming.slots.size() && is_persistent(incoming.slots[id].scope);
        if (was_persistent != is_retained ||
            (was_persistent && current[id].key != incoming.slots[id].key)) {
            throw std::invalid_argument("Prepared scene changed the persistent material IDs");
        }
    }
}

void World::State::adopt_scene(Model scene, const State& previous) {
    material_context = previous.material_context;
    packed_materials = pack_materials(scene.materials);
    persistent_materials = previous.persistent_materials;
    persistent_materials.resize(scene.materials.size());
    model = std::move(scene);
    update_first_vacant_material();
    entity_gpu.chunks.resize(model.chunks.size());
    revision = previous.revision + 1;
    chunk_revision = previous.chunk_revision + 1;
    geometry_revision = previous.geometry_revision + 1;
    material_revision = previous.material_revision + 1;
    entity_gpu.revision = previous.entity_gpu.revision + 1;
    entity_gpu.voxel_revision = previous.entity_gpu.voxel_revision + 1;
}

Model World::prepare_generation(Model source, MaterialPalette palette,
                                std::vector<std::uint16_t>* material_ids) const {
    validate(source);
    if (source.material_context != 0) {
        if (source.material_context != state_->material_context || !palette.values.empty() ||
            !palette.keys.empty() || material_ids) {
            throw std::invalid_argument("Expected a local palette or a model from this world");
        }
        if (source.material_layout) {
            state_->validate_prepared(source);
        } else {
            source.material_layout = state_->scene_layout();
            source.materials = state_->persistent_materials;
            source.materials.resize(source.material_layout->slots.size());
            validate(source);
        }
        return source;
    }
    auto layout = state_->scene_layout();
    auto materials = state_->persistent_materials;
    materials.resize(layout.slots.size());
    auto ids = bind_palette(source, palette, MaterialScope::scene, layout, materials);
    remap_materials(source, ids);
    source.materials = std::move(materials);
    source.material_context = state_->material_context;
    source.material_layout = std::move(layout);
    if (material_ids) {
        *material_ids = std::move(ids);
    }
    return source;
}

std::span<const Material> World::materials() const {
    return state_->model.materials;
}

bool World::has_material(std::uint16_t id) const {
    return state_->has_material(id);
}

std::uint16_t World::add_material(Material material) {
    static_cast<void>(pack_material(material));
    const auto& current = *state_->model.material_layout;
    auto key = named_material(material.name) ? material.name : std::string{};
    if (!key.empty()) {
        const auto found = std::ranges::find(current.slots, key, &MaterialSlot::key);
        if (found != current.slots.end()) {
            return static_cast<std::uint16_t>(found - current.slots.begin());
        }
    }
    auto layout = current;
    auto baseline = state_->persistent_materials;
    MaterialAllocation allocation(layout, baseline);
    const auto id = allocation.add(std::move(material), MaterialScope::persistent, std::move(key));
    state_->publish_materials(state_->effective_palette(baseline));
    state_->persistent_materials = std::move(baseline);
    state_->model.material_layout = std::move(layout);
    state_->update_first_vacant_material();
    return id;
}

void World::set_material(std::uint16_t index, Material material) {
    if (index == 0 || !has_material(index)) {
        throw std::invalid_argument("Invalid material index");
    }
    auto& slot = state_->model.material_layout->slots[index];
    const auto packed = pack_material(material);
    const bool renamed = material.name != state_->model.materials[index].name;
    std::string key;
    if (renamed && named_material(material.name)) {
        key = material.name;
        const auto& slots = state_->model.material_layout->slots;
        if (!key.empty() && key != slot.key &&
            std::ranges::find(slots, key, &MaterialSlot::key) != slots.end()) {
            throw std::invalid_argument("Duplicate material name: " + material.name);
        }
    }
    if (slot.scope == MaterialScope::persistent) {
        state_->persistent_materials[index] = material;
    } else if (slot.scope == MaterialScope::shared && renamed) {
        state_->persistent_materials[index].name = material.name;
    }
    if (renamed) {
        slot.key = std::move(key);
    }
    state_->packed_materials[index] = packed;
    state_->model.materials[index] = std::move(material);
    ++state_->revision;
    ++state_->material_revision;
}

bool World::set_scene_materials(std::span<const MaterialUpdate> updates) {
    auto palette = state_->model.materials;
    auto& slots = state_->model.material_layout->slots;
    std::vector<bool> seen(palette.size());
    for (const auto& update : updates) {
        const auto id = update.id;
        if (id == 0 || !has_material(id) || seen[id]) {
            throw std::invalid_argument("Invalid or repeated runtime material ID");
        }
        seen[id] = true;
        palette[id] = update.material;
    }
    const bool changed = palette != state_->model.materials;
    if (changed) {
        state_->publish_materials(std::move(palette));
    }
    bool scope_changed = false;
    for (const auto& update : updates) {
        if (slots[update.id].scope == MaterialScope::persistent) {
            slots[update.id].scope = MaterialScope::shared;
            scope_changed = true;
        }
    }
    if (scope_changed && !changed) {
        ++state_->revision;
        ++state_->material_revision;
    }
    return changed;
}

std::vector<Material> World::scene_palette(std::span<const std::uint16_t> material_ids) const {
    const auto active = materials();
    std::vector<Material> palette;
    palette.reserve(material_ids.size());
    for (const auto id : material_ids) {
        if (!has_material(id)) {
            throw std::invalid_argument("Scene palette references an unregistered material ID");
        }
        palette.push_back(active[id]);
    }
    return palette;
}

std::span<const Model> World::models() const {
    return state_->models;
}

std::size_t World::add_model(Model model, MaterialPalette palette) {
    validate(model);
    if (model.material_context != 0) {
        throw std::invalid_argument("Model import requires a local palette");
    }
    auto layout = *state_->model.material_layout;
    auto baseline = state_->persistent_materials;
    const auto ids = bind_palette(model, palette, MaterialScope::persistent, layout, baseline);
    remap_materials(model, ids);
    model.material_context = state_->material_context;
    model.materials = baseline;
    const bool changed = layout.slots != state_->model.material_layout->slots;
    auto effective = changed ? state_->effective_palette(baseline) : std::vector<Material>{};
    state_->models.push_back(std::move(model));
    try {
        if (changed) {
            state_->publish_materials(std::move(effective));
            state_->persistent_materials = std::move(baseline);
            state_->model.material_layout = std::move(layout);
            state_->update_first_vacant_material();
        }
    } catch (...) {
        state_->models.pop_back();
        throw;
    }
    return state_->models.size() - 1;
}

} // namespace engine::world::model
