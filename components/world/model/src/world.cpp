#include "state.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace engine::world::model {
namespace {

IVec3 padded_size(IVec3 size, int groups) {
    static_cast<void>(chunk_size(size));
    if (groups < 1 || groups > 8) {
        throw std::runtime_error("World generation segment must be 1..8 groups");
    }
    const auto segment = groups * 64;
    return {((size.x + segment - 1) / segment) * segment,
            ((size.y + segment - 1) / segment) * segment,
            ((size.z + segment - 1) / segment) * segment};
}

std::vector<std::uint32_t> resize_chunks(const Model& model, IVec3 dimensions) {
    const auto source = chunk_size(model.size);
    std::vector<std::uint32_t> result(volume(dimensions));
    for (std::size_t index = 0; index < model.chunks.size(); ++index) {
        const auto position = unflatten(index, source);
        if (inside(position, dimensions)) {
            result[flatten(position, dimensions)] = model.chunks[index];
        }
    }
    return result;
}

void remap_nodes(Model& model, std::span<const std::uint16_t> mapping) {
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

std::uint16_t map_material(const Material& material, std::vector<Material>& palette,
                           std::uint64_t& anonymous_id) {
    if (!material.name.empty() && !material.name.starts_with('_')) {
        const auto found = std::ranges::find(palette, material.name, &Material::name);
        if (found != palette.end()) {
            return static_cast<std::uint16_t>(found - palette.begin());
        }
    }
    if (palette.size() >= 32768) {
        throw std::runtime_error("NAADF material limit reached");
    }
    auto copy = material;
    if (copy.name.empty() || copy.name.starts_with('_')) {
        copy.name = "_" + std::to_string(anonymous_id++);
    }
    palette.push_back(std::move(copy));
    return static_cast<std::uint16_t>(palette.size() - 1);
}

void apply_palette(Model& model, std::vector<Material> palette, std::uint64_t context,
                   std::uint64_t& anonymous_id) {
    if (model.material_context == context && context != 0) {
        model.materials = std::move(palette);
        return;
    }
    if (palette.empty()) {
        palette.push_back({});
    }
    std::vector<std::uint16_t> mapping(model.materials.size());
    for (std::size_t index = 0; index < mapping.size(); ++index) {
        mapping[index] = map_material(model.materials[index], palette, anonymous_id);
    }
    remap_nodes(model, mapping);
    model.materials = std::move(palette);
    model.material_context = context;
}

void compact_palette(Model& model) {
    std::vector<std::uint16_t> mapping(model.materials.size());
    for (const auto* nodes : {&model.chunks, &model.blocks}) {
        for (auto node : *nodes) {
            if ((node >> 30) == 1) {
                mapping[node & 0x3FFFFFFFU] = 1;
            }
        }
    }
    for (auto word : model.voxels) {
        for (unsigned shift : {0U, 16U}) {
            if (((word >> shift) & 0x8000U) != 0) {
                mapping[(word >> shift) & 0x7FFFU] = 1;
            }
        }
    }
    std::vector<Material> palette(1);
    for (std::size_t index = 1; index < mapping.size(); ++index) {
        if (mapping[index] != 0) {
            mapping[index] = static_cast<std::uint16_t>(palette.size());
            palette.push_back(model.materials[index]);
        }
    }
    remap_nodes(model, mapping);
    model.materials = std::move(palette);
    model.material_context = 0;
}

} // namespace

World::World() : state_(std::make_unique<State>()) {
    static std::atomic<std::uint64_t> context = 1;
    state_->material_context = context.fetch_add(1);
    state_->model.materials.push_back({});
    state_->packed_materials.push_back({});
}
World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

void World::replace(Model model, IVec3 requested_size, int segment_groups) {
    validate(model);
    auto next = std::make_unique<State>();
    next->material_context = state_->material_context;
    next->next_anonymous_id = state_->next_anonymous_id;
    apply_palette(model, state_->model.materials, next->material_context, next->next_anonymous_id);
    next->actual_size = requested_size.x == 0 ? model.size : requested_size;
    const auto padded = padded_size(next->actual_size, segment_groups);
    if (padded.x > 16384 || padded.y > 16384 || padded.z > 16384) {
        throw std::runtime_error("World exceeds NAADF dimensions");
    }
    model = generate_model(std::move(model), next->actual_size, padded);
    model_bounds(model);
    next->model = std::move(model);
    initialize_pool(next->model, next->pool);
    next->compression_factor = block_compression(next->pool);
    for (const auto& material : next->model.materials) {
        next->packed_materials.push_back(pack_material(material));
    }
    next->entity_gpu.chunks.resize(next->model.chunks.size());
    next->bounds.scheduled.resize(next->model.chunks.size());
    next->revision = state_->revision + 1;
    next->chunk_revision = state_->chunk_revision + 1;
    next->geometry_revision = state_->geometry_revision + 1;
    next->material_revision = state_->material_revision + 1;
    next->entity_gpu.revision = state_->entity_gpu.revision + 1;
    next->entity_gpu.voxel_revision = state_->entity_gpu.voxel_revision + 1;
    next->models = std::move(state_->models);
    state_ = std::move(next);
}

Model World::prepare_generation(Model source) {
    validate(source);
    auto anonymous_id = state_->next_anonymous_id;
    apply_palette(source, state_->model.materials, state_->material_context, anonymous_id);
    auto packed = state_->packed_materials;
    for (std::size_t index = packed.size(); index < source.materials.size(); ++index) {
        packed.push_back(pack_material(source.materials[index]));
    }
    if (packed.size() != state_->packed_materials.size()) {
        state_->model.materials = source.materials;
        state_->packed_materials = std::move(packed);
        ++state_->revision;
        ++state_->material_revision;
    }
    state_->next_anonymous_id = anonymous_id;
    return source;
}

Model World::snapshot() const {
    auto result = state_->model;
    result.chunks = resize_chunks(result, chunk_size(state_->actual_size));
    result.size = state_->actual_size;
    compact_palette(result);
    return result;
}

WorldView World::view() const {
    const auto& model = state_->model;
    return {model.size,
            state_->actual_size,
            model.chunks,
            model.blocks,
            model.voxels,
            state_->packed_materials,
            state_->revision,
            state_->chunk_revision,
            state_->geometry_revision,
            state_->material_revision,
            state_->changed_chunks,
            state_->pool.changed_blocks,
            state_->pool.changed_voxels,
            state_->invalidated_chunks,
            state_->gpu_authoritative};
}

IVec3 World::size() const {
    return state_->model.size;
}

IVec3 World::actual_size() const {
    return state_->actual_size;
}

double World::compression_factor() const {
    return state_->compression_factor;
}

std::uint16_t World::at(IVec3 position) const {
    if (!inside(position, size())) {
        return 0;
    }
    const IVec3 chunk{position.x / 16, position.y / 16, position.z / 16};
    const auto found = state_->edits.find(flatten(chunk, chunk_size(size())));
    if (found != state_->edits.end()) {
        return found->second.voxels
                   ? (*found->second.voxels)[flatten(
                         {position.x % 16, position.y % 16, position.z % 16}, {16, 16, 16})]
                   : found->second.material;
    }
    return voxel_at(state_->model, position);
}

void World::set(IVec3 position, std::uint16_t material) {
    if (!inside(position, size())) {
        return;
    }
    if (material >= state_->model.materials.size()) {
        throw std::runtime_error("Selected material is outside palette");
    }
    const IVec3 chunk{position.x / 16, position.y / 16, position.z / 16};
    const auto index = flatten(chunk, chunk_size(size()));
    auto found = state_->edits.find(index);
    if (found == state_->edits.end() || !found->second.voxels) {
        auto dense = std::make_unique<DenseChunk>(chunk_voxels(chunk));
        state_->edits[index].voxels = std::move(dense);
        found = state_->edits.find(index);
    }
    (*found->second
          .voxels)[flatten({position.x % 16, position.y % 16, position.z % 16}, {16, 16, 16})] =
        material;
}

void World::rebuild_bounds() {
    if (state_->gpu_authoritative) {
        state_->invalidated_chunks.clear();
        for (std::size_t index = 0; index < state_->model.chunks.size(); ++index) {
            state_->invalidated_chunks.push_back(static_cast<std::uint32_t>(index));
        }
        ++state_->chunk_revision;
        return;
    }
    if (state_->entities.empty()) {
        chunk_bounds(state_->model.chunks, chunk_size(size()));
    } else {
        std::vector<std::size_t> changed;
        for (std::size_t index = 0; index < state_->entity_gpu.chunks.size(); ++index) {
            if (state_->entity_gpu.chunks[index] != 0) {
                changed.push_back(index);
            }
        }
        invalidate_bounds({state_->model.chunks, state_->entity_gpu.chunks, chunk_size(size())},
                          changed, state_->bounds);
    }
    ++state_->chunk_revision;
}

void World::update_bounds(std::size_t cell_budget) {
    if (state_->gpu_authoritative) {
        return;
    }
    if (advance_bounds({state_->model.chunks, state_->entity_gpu.chunks, chunk_size(size())},
                       state_->bounds, cell_budget)) {
        ++state_->chunk_revision;
    }
}

std::span<const Material> World::materials() const {
    return state_->model.materials;
}

std::uint16_t World::add_material(Material material) {
    const auto packed = pack_material(material);
    const auto index = map_material(material, state_->model.materials, state_->next_anonymous_id);
    if (index < state_->packed_materials.size()) {
        return index;
    }
    state_->packed_materials.push_back(packed);
    ++state_->revision;
    ++state_->material_revision;
    return index;
}

void World::set_material(std::uint16_t index, Material material) {
    if (index == 0 || index >= materials().size()) {
        throw std::runtime_error("Invalid material index");
    }
    state_->packed_materials[index] = pack_material(material);
    state_->model.materials[index] = std::move(material);
    ++state_->revision;
    ++state_->material_revision;
}

std::span<const Model> World::models() const {
    return state_->models;
}

std::size_t World::add_model(Model model) {
    validate(model);
    auto anonymous_id = state_->next_anonymous_id;
    apply_palette(model, state_->model.materials, state_->material_context, anonymous_id);
    auto packed = state_->packed_materials;
    for (std::size_t index = packed.size(); index < model.materials.size(); ++index) {
        packed.push_back(pack_material(model.materials[index]));
    }
    if (state_->model.materials.size() != model.materials.size()) {
        state_->model.materials = model.materials;
        state_->packed_materials = std::move(packed);
        ++state_->revision;
        ++state_->material_revision;
    }
    state_->next_anonymous_id = anonymous_id;
    state_->models.push_back(std::move(model));
    return state_->models.size() - 1;
}

} // namespace engine::world::model
