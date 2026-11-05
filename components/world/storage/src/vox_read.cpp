#include "vox.hpp"

#include <cmath>
#include <stdexcept>

namespace engine::world::storage {
namespace {

model::Vec3 linear_rgb(unsigned red, unsigned green, unsigned blue) {
    const auto channel = [](unsigned value) {
        return static_cast<float>(
            std::pow(static_cast<float>(value) / 255.0F, static_cast<double>(2.2F)));
    };
    return {channel(red), channel(green), channel(blue)};
}

std::vector<model::Material> default_palette() {
    std::vector<model::Material> palette(256);
    for (unsigned index = 0; index < 215; ++index) {
        palette[index + 1].color_base = linear_rgb(
            255 - (index / 36) * 51, 255 - ((index / 6) % 6) * 51, 255 - (index % 6) * 51);
    }
    constexpr std::array<unsigned, 10> shades{238, 221, 187, 170, 136, 119, 85, 68, 34, 17};
    for (unsigned index = 0; index < 40; ++index) {
        const auto value = shades[index % 10];
        const auto group = index / 10;
        palette[index + 216].color_base =
            linear_rgb(group == 0 || group == 3 ? value : 0, group == 1 || group == 3 ? value : 0,
                       group == 2 || group == 3 ? value : 0);
    }
    return palette;
}

void read_palette(VoxScene& scene, Reader& reader) {
    for (unsigned index = 1; index < 256; ++index) {
        const auto red = reader.u8();
        const auto green = reader.u8();
        const auto blue = reader.u8();
        static_cast<void>(reader.u8());
        scene.palette[index].color_base = linear_rgb(red, green, blue);
    }
    static_cast<void>(reader.u32());
}

double numeric(const Dictionary& dictionary, const std::string& key) {
    const auto found = dictionary.find(key);
    if (found == dictionary.end()) {
        return 0;
    }
    const auto result = std::stod(found->second);
    if (!std::isfinite(result)) {
        throw std::runtime_error("Non-finite VOX material property");
    }
    return result;
}

void read_material(VoxScene& scene, Reader& reader) {
    const auto index = reader.u32();
    const auto attributes = vox_dictionary(reader);
    if (index >= scene.palette.size()) {
        return;
    }
    const auto type = attributes.find("_type");
    if (type == attributes.end()) {
        return;
    }
    scene.palette[index].base = model::MaterialBase::diffuse;
    scene.palette[index].color_layer = {};
    if (type->second != "_emit") {
        return;
    }
    const auto flux = 1.0F + static_cast<float>(numeric(attributes, "_flux"));
    const auto emission = static_cast<float>(numeric(attributes, "_emit")) * (flux * flux) * 5.0F;
    if (emission > 0) {
        scene.palette[index].base = model::MaterialBase::emissive;
        scene.palette[index].color_layer.x = emission;
    }
}

void read_voxels(VoxScene& scene, Reader& reader) {
    const auto size = scene.pending_size;
    if (size.x < 1 || size.y < 1 || size.z < 1 || size.x > 256 || size.y > 256 || size.z > 256) {
        throw std::runtime_error("VOX SIZE must precede XYZI with dimensions 1..256");
    }
    VoxShape shape{size, {}};
    const auto count = reader.u32();
    if (count > reader.remaining() / 4) {
        throw std::runtime_error("Truncated VOX XYZI array");
    }
    shape.samples.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const model::IVec3 position{reader.u8(), reader.u8(), reader.u8()};
        const auto material = reader.u8();
        if (position.x >= size.x || position.y >= size.y || position.z >= size.z) {
            throw std::runtime_error("Invalid VOX voxel position or palette index");
        }
        if (material != 0) {
            shape.samples.push_back({position, material});
        }
    }
    scene.shapes.push_back(std::move(shape));
}

void parse_chunk(VoxScene& scene, const std::string& kind, Reader& reader) {
    if (kind == "SIZE") {
        scene.pending_size = {reader.i32(), reader.i32(), reader.i32()};
    } else if (kind == "XYZI") {
        read_voxels(scene, reader);
    } else if (kind == "RGBA") {
        read_palette(scene, reader);
    } else if (kind == "MATL") {
        read_material(scene, reader);
    } else if (kind == "nTRN" || kind == "nGRP" || kind == "nSHP" || kind == "LAYR") {
        read_vox_node(scene, kind, reader);
    }
}

void read_chunks(VoxScene& scene, Reader& reader, unsigned depth) {
    if (depth > 32) {
        throw std::runtime_error("VOX chunk nesting exceeds 32");
    }
    while (reader.remaining() != 0) {
        const auto kind = reader.string(4);
        const auto content_size = reader.u32();
        const auto children_size = reader.u32();
        Reader content{reader.take(content_size)};
        parse_chunk(scene, kind, content);
        Reader children{reader.take(children_size)};
        read_chunks(scene, children, depth + 1);
    }
}

} // namespace

Dictionary vox_dictionary(Reader& reader) {
    const auto count = reader.u32();
    if (count > reader.remaining() / 8) {
        throw std::runtime_error("Truncated VOX dictionary");
    }
    Dictionary result;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto key_length = reader.u32();
        const auto key = reader.string(key_length);
        const auto value_length = reader.u32();
        result[key] = reader.string(value_length);
    }
    return result;
}

VoxScene read_vox_scene(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    Reader reader{bytes};
    if (reader.string(4) != "VOX ") {
        throw std::runtime_error("Not a MagicaVoxel VOX file");
    }
    const auto version = reader.u32();
    if (version < 150 || version > 200) {
        throw std::runtime_error("Unsupported MagicaVoxel file version");
    }
    VoxScene result;
    result.palette = default_palette();
    read_chunks(result, reader, 0);
    if (result.shapes.empty()) {
        throw std::runtime_error("VOX file contains no models");
    }
    return result;
}

} // namespace engine::world::storage
