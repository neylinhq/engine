#include <engine/world/storage/storage.hpp>

#include "codec/zip.hpp"

#include <cmath>
#include <stdexcept>

namespace engine::world::storage {
namespace {

model::Vec3 read_vector(Reader& reader) {
    const auto x = reader.f32();
    const auto y = reader.f32();
    const auto z = reader.f32();
    const model::Vec3 result{x, y, z};
    if (!foundation::geometry::finite(result)) {
        throw std::runtime_error("Non-finite material color");
    }
    return result;
}

model::Material read_material(Reader& reader) {
    model::Material result;
    result.name = reader.terminated();
    result.color_base = read_vector(reader);
    result.color_layer = read_vector(reader);
    const auto base = reader.u32();
    const auto layer = reader.u32();
    result.roughness = reader.f32();
    if (base > 3 || layer == 1 || layer > 3 || !std::isfinite(result.roughness)) {
        throw std::runtime_error("Invalid CVox material record");
    }
    result.base = static_cast<model::MaterialBase>(base);
    result.layer = static_cast<model::MaterialLayer>(layer);
    return result;
}

void write_vector(Writer& writer, model::Vec3 value) {
    writer.f32(static_cast<float>(value.x));
    writer.f32(static_cast<float>(value.y));
    writer.f32(static_cast<float>(value.z));
}

void write_material(Writer& writer, const model::Material& material) {
    writer.terminated(material.name.empty() || material.name.starts_with('_') ? "_"
                                                                              : material.name);
    write_vector(writer, material.color_base);
    write_vector(writer, material.color_layer);
    writer.u32(static_cast<std::uint32_t>(material.base));
    writer.u32(static_cast<std::uint32_t>(material.layer));
    writer.f32(material.roughness);
}

std::vector<std::uint32_t> read_words(Reader& reader, std::uint32_t count) {
    if (count > reader.remaining() / 4) {
        throw std::runtime_error("CVox buffer exceeds remaining file size");
    }
    std::vector<std::uint32_t> result(count);
    for (auto& word : result) {
        word = reader.u32();
    }
    return result;
}

void write_words(Writer& writer, std::span<const std::uint32_t> words, bool voxels) {
    for (auto word : words) {
        if (voxels) {
            const auto first = (word & 0x8000U) != 0 ? word & 0xFFFFU : 0;
            const auto second = (word & 0x80000000U) != 0 ? word & 0xFFFF0000U : 0;
            writer.u32(first | second);
        } else {
            writer.u32((word >> 30) == 0 ? 0 : word);
        }
    }
}

model::Model decode_cvox(Reader& reader) {
    const auto version = reader.i32();
    if (version < 1 || version > 3) {
        throw std::runtime_error("Unsupported CVox version (expected 1, 2 or 3)");
    }
    model::Model result;
    result.size = {reader.i32(), reader.i32(), reader.i32()};
    const auto type_count = reader.u32();
    const auto chunk_count = reader.u32();
    const auto block_count = reader.u32();
    const auto voxel_count = reader.u32();
    const auto word_count = std::uint64_t{chunk_count} + block_count + voxel_count / 2;
    if (type_count == 0 || type_count > 32768 || (voxel_count & 1U) != 0 ||
        word_count * 4 + std::uint64_t{type_count} * 37 > reader.remaining()) {
        throw std::runtime_error("Invalid CVox counts or truncated payload");
    }
    for (std::uint32_t index = 0; index < type_count; ++index) {
        result.materials.push_back(read_material(reader));
    }
    result.chunks = read_words(reader, chunk_count);
    result.blocks = read_words(reader, block_count);
    result.voxels = read_words(reader, voxel_count / 2);
    if (version < 3) {
        for (auto& block : result.blocks) {
            if ((block >> 31) != 0) {
                block = 0x80000000U | ((block & 0x3FFFFFFFU) / 2);
            }
        }
    }
    if (reader.remaining() != 0) {
        throw std::runtime_error("Unexpected trailing CVox payload");
    }
    model::validate(result);
    return result;
}

} // namespace

model::Model load_cvox(const std::filesystem::path& path) {
    const auto archive = read_file(path);
    const auto payload = zip_extract(archive, "data");
    Reader reader{payload};
    auto result = decode_cvox(reader);
    result.name = path_text(path.stem());
    return result;
}

void save_cvox(const std::filesystem::path& path, const model::Model& model) {
    model::validate(model);
    if (model.chunks.size() > 0xFFFFFFFFU || model.blocks.size() > 0xFFFFFFFFU ||
        model.voxels.size() > 0x7FFFFFFFU) {
        throw std::runtime_error("CVox buffer count exceeds 32-bit format range");
    }
    Writer writer;
    writer.i32(3);
    writer.i32(model.size.x);
    writer.i32(model.size.y);
    writer.i32(model.size.z);
    writer.u32(static_cast<std::uint32_t>(model.materials.size()));
    writer.u32(static_cast<std::uint32_t>(model.chunks.size()));
    writer.u32(static_cast<std::uint32_t>(model.blocks.size()));
    writer.u32(static_cast<std::uint32_t>(model.voxels.size() * 2));
    for (const auto& material : model.materials) {
        write_material(writer, material);
    }
    write_words(writer, model.chunks, false);
    write_words(writer, model.blocks, false);
    write_words(writer, model.voxels, true);
    write_file(path, zip_compress(writer.bytes(), "data"));
}

model::Model load(const std::filesystem::path& path, const ImportOptions& options) {
    const auto extension = extension_text(path);
    if (extension == ".cvox") {
        return load_cvox(path);
    }
    if (extension == ".vox") {
        return import_vox(path);
    }
    if (extension == ".vl32") {
        return import_vl32(path, options.palette_size);
    }
    if (extension == ".obj" || extension == ".stl") {
        return import_mesh(path, options);
    }
    throw std::runtime_error("Supported model formats: .cvox, .vox, .vl32, .obj, .stl");
}

} // namespace engine::world::storage
