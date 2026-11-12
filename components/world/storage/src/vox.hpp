#ifndef ENGINE_WORLD_STORAGE_SRC_VOX_HPP
#define ENGINE_WORLD_STORAGE_SRC_VOX_HPP

#include "codec/binary.hpp"

#include <engine/world/model/types.hpp>

#include <array>
#include <map>
#include <set>

namespace engine::world::storage {

using Dictionary = std::map<std::string, std::string>;

struct VoxTransform {
    std::array<int, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
    model::Vec3i translation;
};

struct VoxShape {
    model::Vec3i size;
    std::vector<model::Sample> samples;
};

struct VoxNode {
    std::string kind;
    std::vector<int> children;
    int shape = -1;
    int layer = -1;
    bool hidden = false;
    VoxTransform transform;
};

struct VoxScene {
    std::vector<VoxShape> shapes;
    std::vector<model::Material> palette;
    std::map<int, VoxNode> nodes;
    std::set<int> hidden_layers;
    model::Vec3i pending_size;
};

[[nodiscard]] Dictionary vox_dictionary(Reader& reader);
[[nodiscard]] VoxScene read_vox_scene(const std::filesystem::path& path);
void read_vox_node(VoxScene& scene, const std::string& kind, Reader& reader);

} // namespace engine::world::storage

#endif
