#include "codec/binary.hpp"
#include "import.hpp"

#include "mesh/raster.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace engine::world::storage {
namespace {

using Vec3 = model::Vec3;

Vec3 permute(Vec3 point, const std::string& permutation) {
    if (permutation.size() != 3) {
        throw std::runtime_error("Mesh axis permutation must contain x,y,z exactly once");
    }
    Vec3 result;
    unsigned mask = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto character = static_cast<unsigned char>(permutation[axis]);
        const auto index = static_cast<unsigned>(std::tolower(character) - 'x');
        if (index > 2 || (mask & (1U << index)) != 0) {
            throw std::runtime_error("Invalid mesh axis permutation");
        }
        mask |= 1U << index;
        result[axis] = point[index] * (std::isupper(character) != 0 ? -1 : 1);
    }
    return result;
}

foundation::geometry::Box bounds(std::span<const Triangle> triangles) {
    foundation::geometry::Box result{{1e30, 1e30, 1e30}, {-1e30, -1e30, -1e30}};
    for (const auto& triangle : triangles) {
        for (auto point : triangle.points) {
            if (!foundation::geometry::finite(point)) {
                throw std::runtime_error("Non-finite mesh vertex");
            }
            for (unsigned axis = 0; axis < 3; ++axis) {
                result.lower[axis] = std::min(result.lower[axis], point[axis]);
                result.upper[axis] = std::max(result.upper[axis], point[axis]);
            }
        }
    }
    return result;
}

void scale_mesh(std::vector<Triangle>& triangles, const ImportOptions& options) {
    if (options.max_size < 2 || options.max_size > 8192 || triangles.empty()) {
        throw std::runtime_error("Mesh resolution must be 2..8192 and mesh must contain triangles");
    }
    const auto box = bounds(triangles);
    const auto size = box.upper - box.lower;
    const auto longest = std::max({size.x, size.y, size.z});
    if (longest <= 1e-15) {
        throw std::runtime_error("Mesh has no spatial extent");
    }
    const auto scale = (options.max_size - 0.5) / 2;
    for (auto& triangle : triangles) {
        for (auto& point : triangle.points) {
            const auto unit = (point - box.lower) * (2 / longest) - Vec3{1, 1, 1};
            point = (permute(unit, options.axis_permutation) + Vec3{1, 1, 1}) * scale +
                    Vec3{0.25, 0.25, 0.25};
        }
    }
}

model::Vec3i normalize(std::span<ColorVoxel> voxels) {
    if (voxels.empty()) {
        throw std::runtime_error("Mesh contains no nondegenerate voxelized surface");
    }
    model::Vec3i lower{16384, 16384, 16384};
    model::Vec3i upper{0, 0, 0};
    for (const auto& voxel : voxels) {
        lower = {std::min(lower.x, voxel.position.x), std::min(lower.y, voxel.position.y),
                 std::min(lower.z, voxel.position.z)};
        upper = {std::max(upper.x, voxel.position.x), std::max(upper.y, voxel.position.y),
                 std::max(upper.z, voxel.position.z)};
    }
    for (auto& voxel : voxels) {
        voxel.position = {voxel.position.x - lower.x, voxel.position.y - lower.y,
                          voxel.position.z - lower.z};
    }
    return {upper.x - lower.x + 1, upper.y - lower.y + 1, upper.z - lower.z + 1};
}

} // namespace

model::Model import_mesh(const std::filesystem::path& path, const ImportOptions& options) {
    auto triangles = extension_text(path) == ".obj" ? read_obj(path) : read_stl(path);
    scale_mesh(triangles, options);
    auto voxels = voxelize_mesh(triangles, options.max_size);
    // The converter clips in its own XYZ order. NAADF swaps Y/Z only when
    // reading the resulting VL32 stream; moving this swap before clipping
    // changes the original fragment-count weights at overlapping faces.
    for (auto& voxel : voxels) {
        std::swap(voxel.position.y, voxel.position.z);
    }
    const auto size = normalize(voxels);
    auto result = colored_model(size, voxels, options.palette_size);
    result.name = path_text(path.stem());
    return result;
}

} // namespace engine::world::storage
