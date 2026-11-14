#include <engine/world/storage/storage.hpp>

#include <engine/world/model/world.hpp>

#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool same_vector(engine::world::model::Vec3 left, engine::world::model::Vec3 right) {
    return static_cast<float>(left.x) == static_cast<float>(right.x) &&
           static_cast<float>(left.y) == static_cast<float>(right.y) &&
           static_cast<float>(left.z) == static_cast<float>(right.z);
}

bool same_words(std::span<const std::uint32_t> left, std::span<const std::uint32_t> right,
                bool voxels) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        auto canonical = left[index];
        if (voxels) {
            const auto first = (canonical & 0x8000U) != 0 ? canonical & 0xFFFFU : 0;
            const auto second = (canonical & 0x80000000U) != 0 ? canonical & 0xFFFF0000U : 0;
            canonical = first | second;
        } else if ((canonical >> 30) == 0) {
            canonical = 0;
        }
        if (canonical != right[index]) {
            return false;
        }
    }
    return true;
}

void compare(const engine::world::model::Model& left, const engine::world::model::Model& right) {
    require(left.size == right.size && left.materials.size() == right.materials.size(),
            "CVox header changed on round trip");
    require(same_words(left.chunks, right.chunks, false), "CVox chunk topology changed");
    require(same_words(left.blocks, right.blocks, false), "CVox block topology changed");
    require(same_words(left.voxels, right.voxels, true), "CVox occupied voxel words changed");
    for (std::size_t index = 0; index < left.materials.size(); ++index) {
        const auto& a = left.materials[index];
        const auto& b = right.materials[index];
        require(same_vector(a.color_base, b.color_base) &&
                    same_vector(a.color_layer, b.color_layer) && a.base == b.base &&
                    a.layer == b.layer && a.roughness == b.roughness,
                "CVox material properties changed");
    }
}

void path_fixture(const std::filesystem::path& directory) {
    const std::array<unsigned char, 38> bytes{1, 0, 0,  0, 1,   0,   0,  0, 0, 0,  128, 63, 0,
                                              0, 0, 64, 0, 0,   64,  64, 0, 0, 0,  0,   0,  0,
                                              0, 0, 0,  0, 128, 191, 0,  0, 0, 63, 'A', 0};
    const auto source = directory / "fixture.pnb";
    std::ofstream file(source, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file.close();
    const auto nodes = engine::world::storage::load_path(source);
    require(nodes.size() == 1 && nodes[0].name == "A", "Handwritten PNB name/count incorrect");
    require(nodes[0].position == engine::world::model::Vec3{1, 2, 3} && nodes[0].speed == 0.5F,
            "Handwritten PNB numeric fields incorrect");
    const auto destination = directory / "roundtrip.pnb";
    engine::world::storage::save_path(destination, nodes);
    std::ifstream result(destination, std::ios::binary);
    std::array<unsigned char, 38> actual{};
    result.read(reinterpret_cast<char*>(actual.data()), actual.size());
    require(actual == bytes && result.peek() == std::ifstream::traits_type::eof(),
            "PNB writer differs from original binary format");
}

void small_cvox(const std::filesystem::path& directory) {
    auto model = engine::world::model::build_model({19, 17, 4}, {{}, {}, {}},
                                                   {{{0, 0, 0}, 1}, {{18, 16, 3}, 2}});
    const auto file = directory / "small.cvox";
    engine::world::storage::save_cvox(file, model);
    compare(model, engine::world::storage::load_cvox(file));
}

void external_palette(const std::filesystem::path& directory) {
    using namespace engine::world;
    const auto scene = directory / "small.cvox";
    const auto path = directory / "small.materials.json";
    const auto write_palette = [&](std::string_view first, std::string_view second) {
        std::ofstream stream(path, std::ios::binary);
        stream << R"({"schema":"engine.materials.v1","materials":[
            {"id":0,"name":"","color_base":[0,0,0],"color_layer":[0,0,0],
             "base":"diffuse","layer":"none","roughness":0},
            {"id":1,"name":")"
               << first << R"(","color_base":[1,0,0],"color_layer":[0,0,0],
             "base":"diffuse","layer":"none","roughness":0},
            {"id":2,"name":")"
               << second << R"(","color_base":[0,1,0],"color_layer":[0,0,0],
             "base":"diffuse","layer":"none","roughness":0}
        ]})";
        stream.close();
        require(bool(stream), "Could not write material palette fixture");
    };
    write_palette(R"(\u0430)", R"(\u0431)");
    bool rejected = false;
    try {
        static_cast<void>(storage::load_scene(scene, path));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()).contains("ASCII");
    }
    require(rejected, "External palette accepted names that collapse in CVox");

    write_palette("red", "green");
    model::World world;
    auto source = storage::load_scene(scene, path);
    world.replace(world.prepare_generation(std::move(source.model), {source.palette}), {}, 1);
    const auto saved = directory / "palette.cvox";
    const auto exported = directory / "palette.materials.json";
    std::vector<std::uint16_t> ids;
    storage::save_cvox(saved, world.snapshot(&ids));
    storage::save_materials(exported, world.scene_palette(ids));
    const model::Vec3i first{0, 0, 0};
    const model::Vec3i second{18, 16, 3};
    const auto stored = storage::load_cvox(saved);
    const auto original = storage::load_cvox(scene);
    const auto& first_saved = stored.materials[model::voxel_at(stored, first)];
    const auto& second_saved = stored.materials[model::voxel_at(stored, second)];
    require(first_saved.name == original.materials[model::voxel_at(original, first)].name &&
                second_saved.name == original.materials[model::voxel_at(original, second)].name &&
                first_saved.color_base == model::Vec3{1, 0, 0} &&
                second_saved.color_base == model::Vec3{0, 1, 0},
            "CVox did not retain the binding names and effective properties");
    model::World reopened;
    auto imported = storage::load_scene(saved, exported);
    reopened.replace(reopened.prepare_generation(std::move(imported.model), {imported.palette}), {},
                     1);
    require(reopened.materials()[reopened.at(first)].name == "red" &&
                reopened.materials()[reopened.at(second)].name == "green",
            "Reopening the CVox with its external palette lost the display names");
}

void materials_round_trip(const std::filesystem::path& directory) {
    using namespace engine::world;
    const auto path =
        directory /
        std::filesystem::path{u8"\u043f\u0430\u043b\u0438\u0442\u0440\u0430.materials.json"};
    const auto scene = directory / "small.cvox";
    const std::vector<model::Material> palette{
        {},
        {"light", {0.116928, 0.31, 0.89}, {320, 0, 0}, model::MaterialBase::emissive},
        {"mirror",
         {1.8, 2.3, 2.9},
         {0.25, 0.5, 0.75},
         model::MaterialBase::metallic_rough,
         model::MaterialLayer::metallic_mirror,
         0.053F}};
    storage::save_materials(path, palette);
    require(storage::load_materials(path, palette.size()) == palette,
            "Material palette did not round trip");
    const auto loaded = storage::load_scene(scene, path);
    require(loaded.palette == palette && model::voxel_at(loaded.model, {0, 0, 0}) == 1 &&
                model::voxel_at(loaded.model, {18, 16, 3}) == 2,
            "Exported palette could not be applied to its source scene");

    auto homonyms = palette;
    homonyms[2].name = homonyms[1].name;
    storage::save_materials(path, homonyms);
    require(storage::load_materials(path, homonyms.size()) == homonyms,
            "Display names in a palette were treated as binding keys");

    auto invalid = homonyms;
    invalid[2].name = "invalid";
    invalid[2].name.push_back('\0');
    bool rejected = false;
    try {
        storage::save_materials(path, invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && storage::load_materials(path, homonyms.size()) == homonyms,
            "Rejected export replaced the existing palette");

    auto revised = palette;
    revised[1].color_layer.x = 480;
    storage::save_materials(path, revised);
    require(storage::load_materials(path, revised.size()) == revised,
            "Saving a new palette did not replace the existing file");
}

void reject_lossy_cvox_names(const std::filesystem::path& directory) {
    using namespace engine::world;
    const auto scene = directory / "small.cvox";
    const auto original = storage::load_cvox(scene);
    auto model = original;
    const auto reject = [&](std::string_view message) {
        bool rejected = false;
        try {
            storage::save_cvox(scene, model);
        } catch (const std::invalid_argument& error) {
            rejected = std::string_view(error.what()).contains(message);
        }
        require(rejected, "CVox writer accepted a lossy material name");
    };
    model.materials[1].name = "\u0430";
    reject("ASCII");
    model.materials[1].name = "stone";
    model.materials[2].name = "stone";
    reject("Duplicate CVox material name");
    const auto saved = storage::load_cvox(scene);
    compare(original, saved);
    require(original.materials[1].name == saved.materials[1].name,
            "Rejected CVox save modified the original material name");
}

void ascii_path_names(const std::filesystem::path& directory) {
    const std::vector<engine::world::storage::PathNode> nodes{
        {{1, 2, 3}, {0, 0, -1}, 0.5F, "Тест"}};
    const auto file = directory / "ascii_names.pnb";
    engine::world::storage::save_path(file, nodes);
    std::ifstream stream{file, std::ios::binary};
    stream.seekg(36);
    std::array<char, 5> suffix{};
    stream.read(suffix.data(), suffix.size());
    require(suffix == std::array<char, 5>{'?', '?', '?', '?', 0},
            "PNB names must preserve original Encoding.ASCII fallback");
    require(engine::world::storage::load_path(file).front().name == "????",
            "PNB ASCII names did not reload correctly");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto temporary = std::filesystem::temp_directory_path() / "engine_storage_test";
        std::filesystem::create_directories(temporary);
        small_cvox(temporary);
        external_palette(temporary);
        materials_round_trip(temporary);
        reject_lossy_cvox_names(temporary);
        path_fixture(temporary);
        ascii_path_names(temporary);
        if (argc > 1) {
            const auto model = engine::world::storage::load_cvox(argv[1]);
            const auto destination = temporary / "oasis_roundtrip.cvox";
            engine::world::storage::save_cvox(destination, model);
            compare(model, engine::world::storage::load_cvox(destination));
            std::cout << "Oasis round trip exact: " << model.chunks.size() << " chunks, "
                      << model.blocks.size() << " block words, " << model.voxels.size()
                      << " voxel words\n";
        }
        std::cout << "CVox, external palette and PNB fixture tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
