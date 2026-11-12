#ifndef ENGINE_WORLD_STORAGE_SRC_IMPORT_HPP
#define ENGINE_WORLD_STORAGE_SRC_IMPORT_HPP

#include <engine/world/storage/storage.hpp>

#include "image/image.hpp"

#include <array>
#include <vector>

namespace engine::world::storage {

struct ColorVoxel {
    model::Vec3i position;
    std::uint32_t color = 0;
};

struct Triangle {
    std::array<model::Vec3, 3> points;
    std::uint32_t color = 0xFFFFFFU;
    std::array<TextureCoordinate, 3> coordinates;
    std::shared_ptr<const Image> texture;
};

[[nodiscard]] model::Model colored_model(model::Vec3i size, std::span<const ColorVoxel> voxels,
                                         int palette_size);
[[nodiscard]] std::vector<Triangle> read_obj(const std::filesystem::path& path);
[[nodiscard]] std::vector<Triangle> read_stl(const std::filesystem::path& path);

} // namespace engine::world::storage

#endif
