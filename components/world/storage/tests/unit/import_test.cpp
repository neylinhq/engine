#include <engine/world/storage/storage.hpp>

#include "../../src/codec/binary.hpp"
#include "../../src/image/image.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {

namespace model = engine::world::model;
namespace storage = engine::world::storage;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::size_t count_voxels(const model::Model& value) {
    std::size_t count = 0;
    for (int z = 0; z < value.size.z; ++z) {
        for (int y = 0; y < value.size.y; ++y) {
            for (int x = 0; x < value.size.x; ++x) {
                count += model::voxel_at(value, {x, y, z}) != 0;
            }
        }
    }
    return count;
}

void cvox_versions(const std::filesystem::path& root) {
    for (const auto* name : {"v1.cvox", "v2.cvox", "v3.cvox", "zip64.cvox"}) {
        const auto value = storage::load_cvox(root / name);
        require(value.size == model::IVec3{16, 16, 16}, "CVox fixture dimensions incorrect");
        require(value.blocks[0] == 0x80000020U, "Legacy CVox pointer units not migrated");
        require(count_voxels(value) == 1 && model::voxel_at(value, {0, 0, 0}) == 1,
                "Independent CVox fixture voxel incorrect");
    }
}

void voxel_formats(const std::filesystem::path& root) {
    const auto vox = storage::import_vox(root / "scene.vox");
    require(vox.size == model::IVec3{6, 4, 3}, "VOX transformed scene bounds incorrect");
    require(count_voxels(vox) == 2 && model::voxel_at(vox, {1, 3, 2}) == 1 &&
                model::voxel_at(vox, {5, 3, 2}) == 1,
            "VOX graph translation or Y/Z mapping incorrect");
    require(vox.materials[1].color_base == model::Vec3{1, 0, 0}, "VOX palette incorrect");
    const auto vl32 = storage::import_vl32(root / "pair.vl32");
    require(vl32.size == model::IVec3{2, 1, 1}, "VL32 big-endian signed bounds incorrect");
    const auto red = model::voxel_at(vl32, {0, 0, 0});
    const auto blue = model::voxel_at(vl32, {1, 0, 0});
    require(vl32.materials[red].color_base == model::Vec3{1, 0, 0} &&
                vl32.materials[blue].color_base == model::Vec3{0, 0, 1},
            "VL32 colors or palette indices incorrect");
}

void vox_materials(const std::filesystem::path& root) {
    const auto scene = storage::import_vox(root / "material_scene.vox");
    require(scene.size == model::IVec3{3, 3, 3} && count_voxels(scene) == 4,
            "VOX hidden layer, frame0 selection or index0-empty semantics differ");
    require(model::voxel_at(scene, {2, 0, 0}) == 1 && model::voxel_at(scene, {2, 1, 2}) == 2 &&
                model::voxel_at(scene, {0, 2, 0}) == 216 &&
                model::voxel_at(scene, {0, 0, 2}) == 255,
            "VOX centered quarter-turn transform or vertical axis mapping incorrect");
    require(scene.materials[1].base == model::MaterialBase::emissive &&
                scene.materials[1].color_layer.x == 22.5 && scene.materials[1].roughness == 0,
            "VOX emission gain or original zero roughness differs");
    require(scene.materials[2].base == model::MaterialBase::diffuse &&
                scene.materials[2].color_layer.x == 0,
            "Original VOX importer does not import metal/transmission as emission");
    const auto red = static_cast<float>(std::pow(238.0F / 255.0F, static_cast<double>(2.2F)));
    require(scene.materials[216].color_base == model::Vec3{red, 0, 0},
            "VOX default palette tail or float32 transfer function incorrect");
    const auto parts = storage::import_vox(root / "parts_0.vox");
    require(parts.size == model::IVec3{32, 16, 16} && count_voxels(parts) == 2 &&
                model::voxel_at(parts, {0, 0, 0}) == 1 && model::voxel_at(parts, {31, 15, 15}) == 2,
            "VOX sibling assembly must preserve first nonempty chunk ownership");
    require(parts.materials[1].color_base == model::Vec3{0, 0, 1} &&
                parts.materials[2].color_base == model::Vec3{0, 1, 0},
            "Original multipart VOX importer uses the final file palette");
}

void images(const std::filesystem::path& root) {
    for (const auto* name : {"rgb_0.png", "rgb_1.png", "filtered.png", "rgba16.png"}) {
        const auto image = storage::load_image(root / name);
        require(image->width == 7 && image->height == 6, "PNG dimensions incorrect");
        for (unsigned y = 0; y < image->height; ++y) {
            for (unsigned x = 0; x < image->width; ++x) {
                const auto expected = (((x * 37 + y * 11) % 256) << 16) |
                                      (((x * 5 + y * 41) % 256) << 8) | ((x * 13 + y * 19) % 256);
                require(image->pixels[y * image->width + x] == expected,
                        "PNG filter/interlace/depth decoding differs from independent pixels");
            }
        }
    }
    const auto indexed = storage::load_image(root / "indexed.png");
    const std::array<std::uint32_t, 4> palette{0xFF0000U, 0x00FF00U, 0x0000FFU, 0xFFFF00U};
    for (unsigned index = 0; index < 7 * 6; ++index) {
        require(indexed->pixels[index] == palette[(index % 7) % 4],
                "Packed PNG palette decode failed");
    }
}

void corrupt_images(const std::filesystem::path& root) {
    bool rejected = false;
    try {
        static_cast<void>(storage::load_image(root / "bad_adler.png"));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "PNG zlib checksum corruption was accepted despite valid chunk CRC");
    auto gif = storage::read_file(root / "indexed.gif");
    gif.resize(gif.size() / 2);
    rejected = false;
    try {
        static_cast<void>(storage::decode_gif(gif));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Truncated GIF LZW stream was accepted");
}

void meshes(const std::filesystem::path& root) {
    storage::ImportOptions options;
    options.max_size = 8;
    for (const auto* name : {"plane.obj", "plane.stl", "attributes.stl"}) {
        const auto mesh = storage::import_mesh(root / name, options);
        require(mesh.size == model::IVec3{8, 8, 1}, "Mesh resolution/permutation incorrect");
        require(count_voxels(mesh) == 64, "Conservative mesh surface voxelization has holes");
        if (std::filesystem::path{name}.extension() == ".stl") {
            require(mesh.materials[1].color_base == model::Vec3{1, 1, 1},
                    "Original STL importer ignores color attribute bytes");
        }
    }
    const auto textured = storage::import_mesh(root / "plane.obj", options);
    require(textured.materials.size() == 5, "Textured OBJ did not sample all four fixture colors");
    const auto rectangle = storage::import_mesh(root / "rectangle.obj", options);
    require(rectangle.size == model::IVec3{8, 4, 1} && count_voxels(rectangle) == 32,
            "Mesh anti-bleed transform or occupied-bounds crop differs from original converter");
    require(rectangle.materials[model::voxel_at(rectangle, {0, 0, 0})].color_base ==
                model::Vec3{1, 1, 1},
            "Materialless mesh must use original white default");
    options.axis_permutation = "xZy";
    const auto flipped = storage::import_mesh(root / "rectangle.obj", options);
    require(flipped.size == rectangle.size && count_voxels(flipped) == 32,
            "Mesh axis reflection produced different occupied dimensions");
    options.axis_permutation = "xzy";
    const auto overlap = storage::import_mesh(root / "overlap.obj", options);
    const auto material = model::voxel_at(overlap, {2, 2, 0});
    require(material != 0 && overlap.materials[material].color_base == model::Vec3{1, 0, 0},
            "Mesh overlapping triangles must use greatest original weighted contribution");
}

void texture_sampling() {
    const storage::Image image{3, 2, {1, 2, 3, 4, 5, 6}};
    require(storage::sample_image(image, {0, 0}) == 6,
            "Original integer UV must address last repeat texel");
    require(storage::sample_image(image, {0.75, 0.25}) == 2,
            "Original texture sampling must multiply by dimension minus one");
    require(storage::sample_image(image, {-0.25, 1.25}) == 2,
            "Negative repeated UV coordinates differ from original sampler");
}

void original_mesh_oracle(const std::filesystem::path& root, const char* name, int resolution) {
    storage::ImportOptions options;
    options.max_size = resolution;
    options.axis_permutation = "xyz";
    const auto actual = storage::import_mesh(root / (std::string{name} + ".obj"), options);
    const auto expected = storage::import_vl32(root / (std::string{name} + "_oracle.vl32"));
    require(actual.size == expected.size,
            "Native mesh bounds differ from original converter oracle");
    const auto count = static_cast<std::size_t>(actual.size.x) * actual.size.y * actual.size.z;
    for (std::size_t index = 0; index < count; ++index) {
        const model::IVec3 position{static_cast<int>(index % actual.size.x),
                                    static_cast<int>((index / actual.size.x) % actual.size.y),
                                    static_cast<int>(index / actual.size.x / actual.size.y)};
        const auto left = model::voxel_at(actual, position);
        const auto right = model::voxel_at(expected, position);
        require((left == 0) == (right == 0),
                "Native mesh occupancy differs from original converter oracle");
        require(actual.materials[left].color_base == expected.materials[right].color_base,
                "Native mesh overlap colors differ from original converter clipping order");
    }
}

void exact_image(const std::filesystem::path& root, const char* file, const char* oracle) {
    const auto image = storage::load_image(root / file);
    const auto expected = storage::read_file(root / oracle);
    require(expected.size() == image->pixels.size() * 3, "Image oracle byte count differs");
    for (std::size_t index = 0; index < image->pixels.size(); ++index) {
        const auto color = image->pixels[index];
        const auto red = std::to_integer<unsigned>(expected[index * 3]);
        const auto green = std::to_integer<unsigned>(expected[index * 3 + 1]);
        const auto blue = std::to_integer<unsigned>(expected[index * 3 + 2]);
        require(color == ((red << 16) | (green << 8) | blue),
                "Native image decoder differs from independent RGB fixture");
    }
}

void bitmap_and_gif(const std::filesystem::path& root) {
    constexpr std::array<std::pair<const char*, const char*>, 12> fixtures{
        {{"rgb.bmp", "rgb_bmp.rgb"},
         {"rgba.bmp", "rgba_bmp.rgb"},
         {"indexed.bmp", "indexed_bmp.rgb"},
         {"palette_1.bmp", "palette_1.rgb"},
         {"palette_4.bmp", "palette_4.rgb"},
         {"palette_8_core.bmp", "palette_8_core.rgb"},
         {"rgb555.bmp", "rgb555.rgb"},
         {"rgb565.bmp", "rgb565.rgb"},
         {"bgr32.bmp", "bgr32.rgb"},
         {"indexed.gif", "indexed.rgb"},
         {"indexed_interlaced.gif", "indexed_interlaced.rgb"},
         {"transparent.gif", "transparent.rgb"}}};
    for (const auto& [file, oracle] : fixtures) {
        exact_image(root, file, oracle);
    }
}

void utf8_files(const std::filesystem::path& root) {
    const auto directory =
        std::filesystem::temp_directory_path() / storage::utf8_path("engine_русский_путь");
    std::filesystem::create_directories(directory);
    const auto texture = storage::utf8_path("текстура.png");
    const auto material = storage::utf8_path("материал.mtl");
    const auto mesh = storage::utf8_path("модель.OBJ");
    std::filesystem::copy_file(root / "color.png", directory / texture,
                               std::filesystem::copy_options::overwrite_existing);
    std::ofstream{directory / material} << "newmtl test\nKd 1 1 1\nmap_Kd "
                                        << storage::path_text(texture) << '\n';
    std::ofstream{directory / mesh}
        << "mtllib " << storage::path_text(material)
        << "\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl test\nf 1/1 2/2 3/3\n";
    storage::ImportOptions options;
    options.max_size = 8;
    auto model = storage::load(directory / mesh, options);
    require(model.name == "модель" && count_voxels(model) != 0,
            "UTF-8 mesh/material/texture paths failed");
    const auto saved = directory / storage::utf8_path("сцена.CVOX");
    storage::save_cvox(saved, model);
    const auto loaded = storage::load(saved);
    require(loaded.name == "сцена" && count_voxels(loaded) == count_voxels(model),
            "UTF-8 save path or uppercase file extension failed");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path root = argc > 1 ? argv[1] : ENGINE_WORLD_STORAGE_TEST_RESOURCES;
        cvox_versions(root);
        voxel_formats(root);
        vox_materials(root);
        images(root);
        corrupt_images(root);
        meshes(root);
        texture_sampling();
        original_mesh_oracle(root, "random0", 32);
        original_mesh_oracle(root, "tilted", 64);
        bitmap_and_gif(root);
        utf8_files(root);
        std::cout << "Independent CVox1/2/3+ZIP64, VOX scene, VL32, PNG, textured OBJ and STL "
                     "fixtures passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
