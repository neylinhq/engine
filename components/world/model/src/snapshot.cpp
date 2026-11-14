#include "helpers.hpp"

#include <unordered_map>
#include <utility>

namespace engine::world::model {
namespace {

constexpr std::uint32_t kPayload = 0x3FFFFFFFU;
constexpr std::uint32_t kPointer = 0x80000000U;

class SnapshotBuilder {
  public:
    SnapshotBuilder(const Model& source, Vec3i size)
        : source_(source), used_(source.materials.size()) {
        snapshot_.name = source.name;
        snapshot_.size = size;
        snapshot_.blocks.resize(64);
        snapshot_.voxels.resize(32);
    }

    Model build(std::vector<std::uint16_t>* material_ids) {
        copy_chunks();
        compact_palette(material_ids);
        return std::move(snapshot_);
    }

  private:
    void copy_chunks() {
        const auto dimensions = chunk_size(snapshot_.size);
        const auto stride = chunk_size(source_.size);
        snapshot_.chunks.reserve(volume(dimensions));
        for (int z = 0; z < dimensions.z; ++z) {
            for (int y = 0; y < dimensions.y; ++y) {
                const auto row = flatten({0, y, z}, stride);
                for (int x = 0; x < dimensions.x; ++x) {
                    snapshot_.chunks.push_back(copy_chunk(source_.chunks[row + x]));
                }
            }
        }
    }

    std::uint32_t copy_chunk(std::uint32_t word) {
        if (word >> 30 != 2) {
            return copy_uniform(word);
        }
        const auto offset = word & kPayload;
        const auto target = checked_pointer(snapshot_.blocks.size());
        for (auto block : std::span(source_.blocks).subspan(offset, 64)) {
            snapshot_.blocks.push_back(copy_block(block));
        }
        return kPointer | target;
    }

    std::uint32_t copy_block(std::uint32_t word) {
        if (word >> 30 != 2) {
            return copy_uniform(word);
        }
        const auto offset = word & kPayload;
        if (const auto found = voxels_.find(offset); found != voxels_.end()) {
            return kPointer | found->second;
        }
        const auto target = checked_pointer(snapshot_.voxels.size());
        voxels_.emplace(offset, target);
        for (auto packed : std::span(source_.voxels).subspan(offset, 32)) {
            snapshot_.voxels.push_back(copy_voxel(packed & 0xFFFFU) |
                                       (copy_voxel(packed >> 16) << 16));
        }
        return kPointer | target;
    }

    std::uint32_t copy_uniform(std::uint32_t word) {
        if (word >> 30 != 1) {
            return 0;
        }
        used_[word & kPayload] = true;
        return word;
    }

    std::uint32_t copy_voxel(std::uint32_t cell) {
        if ((cell & 0x8000U) == 0) {
            return 0;
        }
        used_[cell & 0x7FFFU] = true;
        return cell;
    }

    void compact_palette(std::vector<std::uint16_t>* material_ids) {
        const auto& slots = source_.material_layout->slots;
        std::vector<std::uint16_t> mapping(used_.size());
        std::vector<std::uint16_t> ids{0};
        snapshot_.materials.push_back({});
        for (std::size_t id = 1; id < used_.size(); ++id) {
            if (used_[id]) {
                mapping[id] = static_cast<std::uint16_t>(snapshot_.materials.size());
                auto material = source_.materials[id];
                material.name = slots[id].key;
                snapshot_.materials.push_back(std::move(material));
                ids.push_back(static_cast<std::uint16_t>(id));
            }
        }
        remap_materials(snapshot_, mapping);
        if (material_ids) {
            *material_ids = std::move(ids);
        }
    }

    const Model& source_;
    Model snapshot_;
    std::unordered_map<std::uint32_t, std::uint32_t> voxels_;
    std::vector<bool> used_;
};

} // namespace

Model snapshot_model(const Model& model, Vec3i size, std::vector<std::uint16_t>* material_ids) {
    return SnapshotBuilder(model, size).build(material_ids);
}

} // namespace engine::world::model
