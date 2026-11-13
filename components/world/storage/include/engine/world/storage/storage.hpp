#ifndef ENGINE_WORLD_STORAGE_STORAGE_HPP
#define ENGINE_WORLD_STORAGE_STORAGE_HPP

#include <engine/world/model/types.hpp>

#include <filesystem>
#include <span>

namespace engine::world::storage {

struct ImportOptions {
    int max_size = 1024;
    int palette_size = 256;
    std::string axis_permutation = "xzy";
};

[[nodiscard]] model::Model load(const std::filesystem::path& path,
                                const ImportOptions& options = {});
// Apply an optional palette to the scene's local material IDs before registration.
[[nodiscard]] model::Model load_scene(const std::filesystem::path& path,
                                      const std::filesystem::path& materials);
[[nodiscard]] model::Model load_cvox(const std::filesystem::path& path);
void save_cvox(const std::filesystem::path& path, const model::Model& model);
[[nodiscard]] model::Model import_vox(const std::filesystem::path& path);
[[nodiscard]] model::Model import_vl32(const std::filesystem::path& path, int palette_size = 256);
[[nodiscard]] model::Model import_mesh(const std::filesystem::path& path,
                                       const ImportOptions& options);

struct PathNode {
    model::Vec3 position;
    model::Vec3 direction{0, 0, -1};
    float speed = 1;
    std::string name;
};

[[nodiscard]] std::vector<PathNode> load_path(const std::filesystem::path& path);
void save_path(const std::filesystem::path& path, std::span<const PathNode> nodes);

} // namespace engine::world::storage

#endif
