#include <engine/world/storage/storage.hpp>

#include "codec/binary.hpp"
#include "codec/cvox.hpp"

#include <array>
#include <boost/json.hpp>
#include <cmath>
#include <engine/platform/filesystem/filesystem.hpp>
#include <stdexcept>
#include <string_view>

namespace engine::world::storage {
namespace {

namespace json = boost::json;

constexpr std::size_t kMaxPaletteBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxPaletteEntries = 32768;
constexpr double kMaxHalf = 65504;
constexpr std::string_view kSchema = "engine.materials.v1";
constexpr std::array<std::string_view, 4> kBaseNames{"diffuse", "emissive", "metallic_rough",
                                                     "metallic_mirror"};
constexpr std::array<std::string_view, 4> kLayerNames{"none", "", "metallic_rough",
                                                      "metallic_mirror"};

double checked_number(double number) {
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
    return {checked_number(array[0].to_number<double>()),
            checked_number(array[1].to_number<double>()),
            checked_number(array[2].to_number<double>())};
}

template <typename Enum>
Enum read_type(const json::string& name, std::span<const std::string_view> names) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (!names[index].empty() && name == names[index]) {
            return static_cast<Enum>(index);
        }
    }
    throw std::invalid_argument("Unknown material type: " + std::string(name));
}

template <typename Enum>
std::string_view write_type(Enum type, std::span<const std::string_view> names) {
    const auto index = static_cast<std::size_t>(type);
    if (index >= names.size() || names[index].empty()) {
        throw std::invalid_argument("Unknown material type");
    }
    return names[index];
}

model::Material read_material(const json::object& record) {
    if (record.size() != 7) {
        throw std::invalid_argument(
            "Expected id, name, color_base, color_layer, base, layer and roughness");
    }
    const auto& name = record.at("name").as_string();
    return {std::string(name),
            color(record.at("color_base")),
            color(record.at("color_layer")),
            read_type<model::MaterialBase>(record.at("base").as_string(), kBaseNames),
            read_type<model::MaterialLayer>(record.at("layer").as_string(), kLayerNames),
            static_cast<float>(checked_number(record.at("roughness").to_number<double>()))};
}

json::array write_color(model::Vec3 value) {
    return {checked_number(value.x), checked_number(value.y), checked_number(value.z)};
}

json::object write_material(std::size_t id, const model::Material& material) {
    return {{"id", id},
            {"name", material.name},
            {"color_base", write_color(material.color_base)},
            {"color_layer", write_color(material.color_layer)},
            {"base", write_type(material.base, kBaseNames)},
            {"layer", write_type(material.layer, kLayerNames)},
            {"roughness", checked_number(material.roughness)}};
}

void validate_path(const std::filesystem::path& path) {
    if (extension_text(path) != ".json" || extension_text(path.stem()) != ".materials") {
        throw std::invalid_argument("Expected a *.materials.json file");
    }
}

void validate_count(std::size_t count) {
    if (count == 0 || count > kMaxPaletteEntries) {
        throw std::invalid_argument("Material palette must contain 1..32768 entries");
    }
}

json::value read_document(const std::filesystem::path& path) {
    const auto data = platform::filesystem::read(path, kMaxPaletteBytes);
    if (data.empty()) {
        throw std::invalid_argument("Material palette is empty");
    }
    const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    return json::parse(text, {}, {.numbers = json::number_precision::precise});
}

std::vector<model::Material> decode_palette(const json::array& records, std::size_t count) {
    if (records.size() != count) {
        throw std::invalid_argument("Expected " + std::to_string(count) +
                                    " material records for this scene");
    }
    std::vector<model::Material> palette(count);
    std::vector<bool> assigned(count);
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
        assigned[id] = true;
        auto& entry = palette[id];
        entry = read_material(record);
        validate_cvox_material_name(entry.name);
    }
    return palette;
}

} // namespace

std::vector<model::Material> load_materials(const std::filesystem::path& path, std::size_t count) {
    try {
        validate_path(path);
        validate_count(count);
        const auto document = read_document(path);
        const auto& root = document.as_object();
        if (root.size() != 2 || root.at("schema").as_string() != kSchema) {
            throw std::invalid_argument(
                "Expected schema engine.materials.v1 and a materials array");
        }
        return decode_palette(root.at("materials").as_array(), count);
    } catch (const std::exception& error) {
        throw std::runtime_error("Cannot load materials " + path_text(path) + ": " + error.what());
    }
}

void save_materials(const std::filesystem::path& path, std::span<const model::Material> materials) {
    validate_path(path);
    validate_count(materials.size());
    constexpr std::string_view footer = "\n  ]\n}\n";
    auto text =
        "{\n  \"schema\": " + json::serialize(json::value(kSchema)) + ",\n  \"materials\": [\n";
    for (std::size_t id = 0; id < materials.size(); ++id) {
        validate_cvox_material_name(materials[id].name);
        if (id != 0) {
            text += ",\n";
        }
        text += "    ";
        text += json::serialize(write_material(id, materials[id]));
        if (text.size() > kMaxPaletteBytes - footer.size()) {
            throw std::invalid_argument("Material palette exceeds 16 MiB");
        }
    }
    text += footer;
    platform::filesystem::write(path, std::as_bytes(std::span(text)));
}

SceneSource load_scene(const std::filesystem::path& path, const std::filesystem::path& materials) {
    auto scene = load(path);
    auto palette = materials.empty() ? std::vector<model::Material>{}
                                     : load_materials(materials, scene.materials.size());
    return {std::move(scene), std::move(palette)};
}

} // namespace engine::world::storage
