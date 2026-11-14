#include "helpers.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace engine::world::model {

Vec3i chunk_size(Vec3i size) {
    if (size.x < 1 || size.y < 1 || size.z < 1 || size.x > 16384 || size.y > 16384 ||
        size.z > 16384) {
        throw std::runtime_error("NAADF dimensions must be in 1..16384");
    }
    return {(size.x + 15) / 16, (size.y + 15) / 16, (size.z + 15) / 16};
}

std::size_t volume(Vec3i size) {
    if (size.x < 1 || size.y < 1 || size.z < 1) {
        throw std::runtime_error("Volume dimensions must be positive");
    }
    const auto x = static_cast<std::size_t>(size.x);
    const auto y = static_cast<std::size_t>(size.y);
    const auto z = static_cast<std::size_t>(size.z);
    if (x > std::numeric_limits<std::size_t>::max() / y / z) {
        throw std::runtime_error("Volume dimensions overflow address space");
    }
    return x * y * z;
}

std::size_t flatten(Vec3i position, Vec3i size) {
    return static_cast<std::size_t>(position.x) +
           static_cast<std::size_t>(size.x) *
               (static_cast<std::size_t>(position.y) +
                static_cast<std::size_t>(size.y) * static_cast<std::size_t>(position.z));
}

Vec3i unflatten(std::size_t index, Vec3i size) {
    const auto x = static_cast<std::size_t>(size.x);
    const auto y = static_cast<std::size_t>(size.y);
    return {static_cast<int>(index % x), static_cast<int>((index / x) % y),
            static_cast<int>(index / (x * y))};
}

bool inside(Vec3i position, Vec3i size) {
    return position.x >= 0 && position.y >= 0 && position.z >= 0 && position.x < size.x &&
           position.y < size.y && position.z < size.z;
}

std::uint32_t checked_pointer(std::size_t value) {
    if (value > 0x3FFFFFFFU) {
        throw std::runtime_error("NAADF buffer exceeds 30-bit pointer range");
    }
    return static_cast<std::uint32_t>(value);
}

std::uint16_t voxel_at(const Model& model, Vec3i position) {
    if (!inside(position, model.size)) {
        return 0;
    }
    const Vec3i chunk{position.x / 16, position.y / 16, position.z / 16};
    auto word = model.chunks[flatten(chunk, chunk_size(model.size))];
    if ((word >> 30) != 2) {
        return (word >> 30) == 1 ? static_cast<std::uint16_t>(word & 0x7FFFU) : 0;
    }
    const Vec3i block{position.x % 16 / 4, position.y % 16 / 4, position.z % 16 / 4};
    word = model.blocks[(word & 0x3FFFFFFFU) + flatten(block, {4, 4, 4})];
    if ((word >> 30) != 2) {
        return (word >> 30) == 1 ? static_cast<std::uint16_t>(word & 0x7FFFU) : 0;
    }
    const auto index = flatten({position.x % 4, position.y % 4, position.z % 4}, {4, 4, 4});
    const auto packed = model.voxels[(word & 0x3FFFFFFFU) + index / 2];
    const auto value = static_cast<std::uint16_t>(packed >> (16 * (index & 1)));
    return (value & 0x8000U) != 0 ? static_cast<std::uint16_t>(value & 0x7FFFU) : 0;
}

namespace {

bool valid_material(const Model& model, std::uint32_t id) {
    return id < model.materials.size() &&
           (!model.material_layout ||
            model.material_layout->slots[id].scope != MaterialScope::vacant);
}

void validate_node(std::uint32_t word, std::size_t count, std::size_t width, const Model& model) {
    const auto state = word >> 30;
    const auto payload = word & 0x3FFFFFFFU;
    if (state == 3 || (state == 1 && !valid_material(model, payload))) {
        throw std::runtime_error("Invalid NAADF node state or material");
    }
    if (state == 2 && (payload > count || width > count - payload)) {
        throw std::runtime_error("NAADF node pointer is outside its buffer");
    }
}

void apply_sample(std::unordered_map<std::size_t, DenseChunk>& chunks, const Sample& sample,
                  Vec3i dimensions) {
    const Vec3i chunk{sample.position.x / 16, sample.position.y / 16, sample.position.z / 16};
    const Vec3i local{sample.position.x % 16, sample.position.y % 16, sample.position.z % 16};
    chunks[flatten(chunk, dimensions)][flatten(local, {16, 16, 16})] = sample.material;
}

} // namespace

void validate(const Model& model) {
    if (model.size.x > 16384 || model.size.y > 16384 || model.size.z > 16384) {
        throw std::runtime_error("NAADF dimensions exceed 16384 voxels");
    }
    if (model.chunks.size() != volume(chunk_size(model.size))) {
        throw std::runtime_error("Chunk count does not match model dimensions");
    }
    if (model.materials.empty() || model.materials.size() > 32768) {
        throw std::runtime_error("Material palette must contain 1..32768 entries");
    }
    if (model.material_layout) {
        const auto& slots = model.material_layout->slots;
        if (model.material_context == 0 || slots.size() != model.materials.size() ||
            slots[0].scope != MaterialScope::persistent) {
            throw std::runtime_error("Invalid material slot table");
        }
    }
    for (auto word : model.chunks) {
        validate_node(word, model.blocks.size(), 64, model);
    }
    for (auto word : model.blocks) {
        validate_node(word, model.voxels.size(), 32, model);
    }
    for (auto word : model.voxels) {
        for (unsigned shift : {0U, 16U}) {
            const auto voxel = (word >> shift) & 0xFFFFU;
            if ((voxel & 0x8000U) != 0 && !valid_material(model, voxel & 0x7FFFU)) {
                throw std::runtime_error("Voxel material is outside palette");
            }
        }
    }
}

Model build_model(Vec3i size, std::vector<Material> materials, const std::vector<Sample>& samples) {
    Model result;
    result.size = size;
    result.materials = std::move(materials);
    result.chunks.resize(volume(chunk_size(size)));
    result.blocks.resize(64);
    result.voxels.resize(32);
    std::unordered_map<std::size_t, DenseChunk> dense;
    for (const auto& sample : samples) {
        if (!inside(sample.position, size) || sample.material >= result.materials.size()) {
            throw std::runtime_error("Imported voxel lies outside dimensions or palette");
        }
        apply_sample(dense, sample, chunk_size(size));
    }
    BlockPool pool;
    for (const auto& [index, chunk] : dense) {
        result.chunks[index] = encode_chunk(result, chunk, pool);
    }
    validate(result);
    return result;
}

} // namespace engine::world::model
