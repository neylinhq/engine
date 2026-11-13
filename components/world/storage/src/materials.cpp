#include <engine/world/storage/storage.hpp>

#include "codec/binary.hpp"
#include "codec/cvox.hpp"

#include <boost/json.hpp>
#include <cmath>
#include <engine/platform/filesystem/filesystem.hpp>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace engine::world::storage {
namespace {

namespace json = boost::json;

double material_number(const json::value& value) {
    constexpr double kMaxHalf = 65504;
    const auto number = value.to_number<double>();
    if (!std::isfinite(number) || std::abs(number) > kMaxHalf) {
        throw std::invalid_argument("Material number is outside the finite GPU range");
    }
    return number;
}

model::Vec3 color(const json::value& value) {
    const auto& array = value.as_array();
    if (array.size() != 3) {
        throw std::invalid_argument("Material color requires three components");
    }
    return {material_number(array[0]), material_number(array[1]), material_number(array[2])};
}

model::MaterialBase base(const json::string& name) {
    using enum model::MaterialBase;
    if (name == "diffuse") {
        return diffuse;
    }
    if (name == "emissive") {
        return emissive;
    }
    if (name == "metallic_rough") {
        return metallic_rough;
    }
    if (name == "metallic_mirror") {
        return metallic_mirror;
    }
    throw std::invalid_argument("Unknown material base: " + std::string(name));
}

model::MaterialLayer layer(const json::string& name) {
    using enum model::MaterialLayer;
    if (name == "none") {
        return none;
    }
    if (name == "metallic_rough") {
        return metallic_rough;
    }
    if (name == "metallic_mirror") {
        return metallic_mirror;
    }
    throw std::invalid_argument("Unknown material layer: " + std::string(name));
}

model::Material material(const json::object& record) {
    if (record.size() != 7) {
        throw std::invalid_argument(
            "Expected id, name, color_base, color_layer, base, layer and roughness");
    }
    const auto& name = record.at("name").as_string();
    validate_cvox_material_name(std::string_view(name.data(), name.size()));
    return {std::string(name),
            color(record.at("color_base")),
            color(record.at("color_layer")),
            base(record.at("base").as_string()),
            layer(record.at("layer").as_string()),
            static_cast<float>(material_number(record.at("roughness")))};
}

std::vector<model::Material> load_palette(const std::filesystem::path& path, std::size_t count) {
    if (extension_text(path) != ".json" || extension_text(path.stem()) != ".materials") {
        throw std::invalid_argument("Expected a *.materials.json file");
    }
    constexpr std::size_t kMaxPaletteBytes = 16 * 1024 * 1024;
    const auto data = platform::filesystem::read(path, kMaxPaletteBytes);
    if (data.empty()) {
        throw std::invalid_argument("Material palette is empty");
    }
    const auto document =
        json::parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    const auto& root = document.as_object();
    if (root.size() != 2 || root.at("schema").as_string() != "engine.materials.v1") {
        throw std::invalid_argument("Expected schema engine.materials.v1 and a materials array");
    }
    const auto& records = root.at("materials").as_array();
    if (records.size() != count) {
        throw std::invalid_argument("Expected " + std::to_string(count) +
                                    " material records for this scene");
    }
    std::vector<model::Material> palette(count);
    std::vector<bool> assigned(count);
    std::unordered_set<std::string> names;
    for (const auto& value : records) {
        const auto& record = value.as_object();
        const auto id = record.at("id").to_number<std::size_t>();
        if (id >= count) {
            throw std::invalid_argument("Material ID is outside the scene palette: " +
                                        std::to_string(id));
        }
        if (assigned[id]) {
            throw std::invalid_argument("Duplicate material ID: " + std::to_string(id));
        }
        auto entry = material(record);
        if (!entry.name.empty() && !entry.name.starts_with('_') &&
            !names.insert(entry.name).second) {
            throw std::invalid_argument("Duplicate material name: " + entry.name);
        }
        assigned[id] = true;
        palette[id] = std::move(entry);
    }
    return palette;
}

} // namespace

model::Model load_scene(const std::filesystem::path& path, const std::filesystem::path& materials) {
    auto scene = load(path);
    if (!materials.empty()) {
        try {
            scene.materials = load_palette(materials, scene.materials.size());
        } catch (const std::exception& error) {
            throw std::runtime_error("Cannot load materials " + path_text(materials) + ": " +
                                     error.what());
        }
    }
    return scene;
}

} // namespace engine::world::storage
