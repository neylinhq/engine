#include "helpers.hpp"

#include <algorithm>

namespace engine::world::model {
namespace {

std::uint32_t copy_node(Model& destination, const Model& source, std::uint32_t word) {
    if ((word >> 30) == 0) {
        return 0;
    }
    if ((word >> 30) == 1) {
        return word;
    }
    const auto offset = checked_pointer(destination.blocks.size());
    const auto first = source.blocks.begin() + (word & 0x3FFFFFFFU);
    destination.blocks.insert(destination.blocks.end(), first, first + 64);
    return 0x80000000U | offset;
}

bool boundary(Vec3i chunk, Vec3i actual_size) {
    return (chunk.x + 1) * 16 > actual_size.x || (chunk.y + 1) * 16 > actual_size.y ||
           (chunk.z + 1) * 16 > actual_size.z;
}

void clip_chunk(Model& model, std::size_t index, Vec3i actual_size, BlockPool& pool) {
    auto dense = decode_chunk(model, index);
    const auto chunk = unflatten(index, chunk_size(model.size));
    for (std::size_t voxel = 0; voxel < dense.size(); ++voxel) {
        const auto local = unflatten(voxel, {16, 16, 16});
        const Vec3i position{chunk.x * 16 + local.x, chunk.y * 16 + local.y,
                             chunk.z * 16 + local.z};
        if (!inside(position, actual_size)) {
            dense[voxel] = 0;
        }
    }
    const auto old = model.chunks[index];
    model.chunks[index] = encode_chunk(model, dense, pool);
    release_chunk(model, old, pool);
}

void clip_world_boundary(Model& model, Vec3i actual_size) {
    if (actual_size.x % 16 == 0 && actual_size.y % 16 == 0 && actual_size.z % 16 == 0) {
        return;
    }
    BlockPool pool;
    initialize_pool(model, pool);
    for (std::size_t index = 0; index < model.chunks.size(); ++index) {
        const auto chunk = unflatten(index, chunk_size(model.size));
        if ((model.chunks[index] >> 30) != 0 && boundary(chunk, actual_size)) {
            clip_chunk(model, index, actual_size, pool);
        }
    }
}

} // namespace

Model generate_model(Model source, Vec3i actual_size, Vec3i padded_size) {
    Model result;
    result.name = source.name;
    result.material_context = source.material_context;
    result.material_layout = std::move(source.material_layout);
    result.size = padded_size;
    result.materials = std::move(source.materials);
    result.voxels = std::move(source.voxels);
    result.blocks.resize(64);
    const auto source_size = chunk_size(source.size);
    const auto dimensions = chunk_size(padded_size);
    result.chunks.resize(volume(dimensions));
    for (std::size_t index = 0; index < result.chunks.size(); ++index) {
        const auto position = unflatten(index, dimensions);
        const Vec3i origin{position.x * 16, position.y * 16, position.z * 16};
        if (!inside(origin, actual_size) || position.y >= source_size.y) {
            continue;
        }
        const Vec3i repeated{position.x % source_size.x, position.y, position.z % source_size.z};
        result.chunks[index] =
            copy_node(result, source, source.chunks[flatten(repeated, source_size)]);
    }
    clip_world_boundary(result, actual_size);
    return result;
}

} // namespace engine::world::model
