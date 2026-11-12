#include "vox.hpp"

#include <engine/world/storage/storage.hpp>

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace engine::world::storage {
namespace {

bool hidden(const Dictionary& values) {
    const auto found = values.find("_hidden");
    return found != values.end() && found->second == "1";
}

VoxTransform frame_transform(const Dictionary& values) {
    VoxTransform result;
    if (auto found = values.find("_t"); found != values.end()) {
        std::istringstream text{found->second};
        if (!(text >> result.translation.x >> result.translation.y >> result.translation.z)) {
            throw std::runtime_error("Invalid VOX transform translation");
        }
    }
    if (auto found = values.find("_r"); found != values.end()) {
        const auto packed = static_cast<unsigned>(std::stoul(found->second));
        const auto first = packed & 3U;
        const auto second = (packed >> 2) & 3U;
        if (first > 2 || second > 2 || first == second || packed > 127) {
            throw std::runtime_error("Invalid VOX rotation encoding");
        }
        const std::array<unsigned, 3> columns{first, second, 3U - first - second};
        result.rotation.fill(0);
        for (unsigned row = 0; row < 3; ++row) {
            result.rotation[row * 3 + columns[row]] = (packed & (16U << row)) != 0 ? -1 : 1;
        }
    }
    return result;
}

model::Vec3i transform(model::Vec3i point, const VoxTransform& matrix) {
    const std::array<int, 3> coordinates{point.x, point.y, point.z};
    std::array<std::int64_t, 3> result{matrix.translation.x, matrix.translation.y,
                                       matrix.translation.z};
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            result[row] +=
                static_cast<std::int64_t>(matrix.rotation[row * 3 + column]) * coordinates[column];
        }
        if (std::abs(result[row]) > 1000000000) {
            throw std::runtime_error("VOX transform exceeds coordinate bounds");
        }
    }
    return {static_cast<int>(result[0]), static_cast<int>(result[1]), static_cast<int>(result[2])};
}

VoxTransform compose(const VoxTransform& parent, const VoxTransform& local) {
    VoxTransform result;
    result.rotation.fill(0);
    result.translation = transform(local.translation, parent);
    for (unsigned index = 0; index < 9; ++index) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            result.rotation[index] +=
                parent.rotation[(index / 3) * 3 + axis] * local.rotation[axis * 3 + index % 3];
        }
    }
    return result;
}

struct Flattened {
    std::vector<model::Sample> samples;
    model::Vec3i lower{1000000000, 1000000000, 1000000000};
    model::Vec3i upper{-1000000000, -1000000000, -1000000000};
};

void include(Flattened& output, model::Vec3i position) {
    output.lower = {std::min(output.lower.x, position.x), std::min(output.lower.y, position.y),
                    std::min(output.lower.z, position.z)};
    output.upper = {std::max(output.upper.x, position.x), std::max(output.upper.y, position.y),
                    std::max(output.upper.z, position.z)};
}

void append_shape(Flattened& output, const VoxShape& shape, VoxTransform matrix, bool centered) {
    const model::Vec3i origin =
        centered ? model::Vec3i{-shape.size.x / 2, -shape.size.y / 2, -shape.size.z / 2}
                 : model::Vec3i{};
    include(output, transform(origin, matrix));
    include(output, transform({origin.x + shape.size.x - 1, origin.y + shape.size.y - 1,
                               origin.z + shape.size.z - 1},
                              matrix));
    for (const auto& sample : shape.samples) {
        const auto position = sample.position;
        const auto placed = transform(
            {origin.x + position.x, origin.y + position.y, origin.z + position.z}, matrix);
        output.samples.push_back({placed, sample.material});
    }
}

struct Visit {
    int node = 0;
    VoxTransform matrix;
    std::set<int> ancestors;
};

void flatten_node(const VoxScene& scene, Flattened& output, Visit visit) {
    if (!visit.ancestors.insert(visit.node).second || visit.ancestors.size() > 128) {
        throw std::runtime_error("VOX scene graph cycle or excessive nesting");
    }
    const auto& node = scene.nodes.at(visit.node);
    if (node.hidden || scene.hidden_layers.contains(node.layer)) {
        return;
    }
    if (node.kind == "nTRN") {
        visit.matrix = compose(visit.matrix, node.transform);
    }
    if (node.shape >= 0) {
        append_shape(output, scene.shapes.at(static_cast<std::size_t>(node.shape)), visit.matrix,
                     true);
    }
    for (auto child : node.children) {
        flatten_node(scene, output, {child, visit.matrix, visit.ancestors});
    }
}

void transform_node(VoxNode& node, Reader& reader) {
    node.children.push_back(reader.i32());
    static_cast<void>(reader.i32());
    node.layer = reader.i32();
    const auto frames = reader.u32();
    if (frames == 0 || frames > reader.remaining() / 4) {
        throw std::runtime_error("Invalid VOX transform frame count");
    }
    for (unsigned index = 0; index < frames; ++index) {
        const auto values = vox_dictionary(reader);
        if (index == 0) {
            node.transform = frame_transform(values);
        }
    }
}

void group_or_shape(VoxNode& node, Reader& reader) {
    const auto count = reader.u32();
    if (count > reader.remaining() / 4) {
        throw std::runtime_error("Invalid VOX node child count");
    }
    for (unsigned index = 0; index < count; ++index) {
        const auto child = reader.i32();
        if (node.kind == "nGRP") {
            node.children.push_back(child);
        } else {
            static_cast<void>(vox_dictionary(reader));
            if (index == 0) {
                node.shape = child;
            }
        }
    }
}

std::vector<std::filesystem::path> related_files(const std::filesystem::path& path) {
    auto name = path_text(path.filename());
    if (!name.ends_with("_0.vox")) {
        return {path};
    }
    name.resize(name.size() - 5);
    std::vector<std::filesystem::path> result;
    const auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vox" &&
            path_text(entry.path().filename()).starts_with(name)) {
            result.push_back(entry.path());
        }
    }
    std::ranges::sort(result);
    return result;
}

using ChunkOwners = std::map<std::uint64_t, std::size_t>;

void append_file(Flattened& output, const Flattened& source, std::size_t file_index,
                 ChunkOwners& owners) {
    const model::Vec3i offset{(source.lower.x - output.lower.x) / 16 * 16,
                              (source.lower.z - output.lower.z) / 16 * 16,
                              (source.lower.y - output.lower.y) / 16 * 16};
    for (const auto& sample : source.samples) {
        const auto local = sample.position;
        const model::Vec3i position{offset.x + local.x - source.lower.x,
                                    offset.y + local.z - source.lower.z,
                                    offset.z + local.y - source.lower.y};
        const auto key = static_cast<std::uint64_t>(position.x / 16) |
                         (static_cast<std::uint64_t>(position.y / 16) << 21) |
                         (static_cast<std::uint64_t>(position.z / 16) << 42);
        const auto [iterator, inserted] = owners.try_emplace(key, file_index);
        if (inserted || iterator->second == file_index) {
            output.samples.push_back({position, sample.material});
        }
    }
}

} // namespace

void read_vox_node(VoxScene& scene, const std::string& kind, Reader& reader) {
    const auto id = reader.i32();
    const auto attributes = vox_dictionary(reader);
    if (kind == "LAYR") {
        if (hidden(attributes)) {
            scene.hidden_layers.insert(id);
        }
        return;
    }
    VoxNode node;
    node.kind = kind;
    node.hidden = kind == "nTRN" && hidden(attributes);
    if (kind == "nTRN") {
        transform_node(node, reader);
    } else {
        group_or_shape(node, reader);
    }
    scene.nodes.emplace(id, std::move(node));
}

model::Model import_vox(const std::filesystem::path& path) {
    Flattened flattened;
    std::vector<model::Material> palette;
    std::vector<Flattened> files;
    for (const auto& file : related_files(path)) {
        const auto scene = read_vox_scene(file);
        palette = scene.palette;
        Flattened content;
        if (scene.nodes.empty()) {
            append_shape(content, scene.shapes.front(), {}, false);
        } else {
            flatten_node(scene, content, {scene.nodes.begin()->first, {}, {}});
        }
        include(flattened, content.lower);
        include(flattened, content.upper);
        files.push_back(std::move(content));
    }
    const model::Vec3i size{flattened.upper.x - flattened.lower.x + 1,
                            flattened.upper.z - flattened.lower.z + 1,
                            flattened.upper.y - flattened.lower.y + 1};
    if (size.x < 1 || size.y < 1 || size.z < 1 || size.x > 16384 || size.y > 16384 ||
        size.z > 16384) {
        throw std::runtime_error("Empty or oversized VOX scene");
    }
    ChunkOwners owners;
    for (std::size_t index = 0; index < files.size(); ++index) {
        append_file(flattened, files[index], index, owners);
    }
    auto result = model::build_model(size, std::move(palette), flattened.samples);
    result.name = path_text(path.stem());
    return result;
}

} // namespace engine::world::storage
