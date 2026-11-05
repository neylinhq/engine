#include "codec/binary.hpp"
#include "import.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace engine::world::storage {

model::Model import_vl32(const std::filesystem::path& path, int palette_size) {
    const auto bytes = read_file(path);
    if (bytes.empty() || bytes.size() % 16 != 0) {
        throw std::runtime_error("VL32 must contain complete 16-byte voxel records");
    }
    Reader reader{bytes};
    std::vector<ColorVoxel> voxels;
    voxels.reserve(bytes.size() / 16);
    model::IVec3 lower{std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
                       std::numeric_limits<int>::max()};
    model::IVec3 upper{std::numeric_limits<int>::min(), std::numeric_limits<int>::min(),
                       std::numeric_limits<int>::min()};
    while (reader.remaining() != 0) {
        const auto x = reader.big_i32();
        const auto y = reader.big_i32();
        const auto z = reader.big_i32();
        static_cast<void>(reader.u8());
        const auto red = reader.u8();
        const auto green = reader.u8();
        const auto blue = reader.u8();
        voxels.push_back(
            {{x, z, y}, (std::uint32_t{red} << 16) | (std::uint32_t{green} << 8) | blue});
        lower = {std::min(lower.x, x), std::min(lower.y, z), std::min(lower.z, y)};
        upper = {std::max(upper.x, x), std::max(upper.y, z), std::max(upper.z, y)};
    }
    const auto dimension = [](int low, int high) {
        const auto difference = static_cast<std::int64_t>(high) - low + 1;
        if (difference > 16384) {
            throw std::runtime_error("VL32 model dimension exceeds NAADF limit");
        }
        return static_cast<int>(difference);
    };
    const model::IVec3 size{dimension(lower.x, upper.x), dimension(lower.y, upper.y),
                            dimension(lower.z, upper.z)};
    for (auto& voxel : voxels) {
        voxel.position = {voxel.position.x - lower.x, voxel.position.y - lower.y,
                          voxel.position.z - lower.z};
    }
    auto result = colored_model(size, voxels, palette_size);
    result.name = path_text(path.stem());
    return result;
}

} // namespace engine::world::storage
