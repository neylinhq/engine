#pragma once

#include <engine/world/storage/storage.hpp>

#include "image/image.hpp"

#include <array>
#include <vector>

namespace engine::world::storage {

struct ColorVoxel {
    model::IVec3 position;
    std::uint32_t color = 0;
};

struct Triangle {
    std::array<model::Vec3, 3> points;
    std::uint32_t color = 0xFFFFFFU;
    std::array<TextureCoordinate, 3> coordinates;
    std::shared_ptr<const Image> texture;
};

[[nodiscard]] model::Model colored_model(model::IVec3 size, std::span<const ColorVoxel> voxels,
                                         int palette_size);
[[nodiscard]] std::vector<Triangle> read_obj(const std::filesystem::path& path);
[[nodiscard]] std::vector<Triangle> read_stl(const std::filesystem::path& path);

} // namespace engine::world::storage
