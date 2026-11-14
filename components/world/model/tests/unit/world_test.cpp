#include <engine/world/model/world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {

using engine::world::model::Sample;
using engine::world::model::Vec3;
using engine::world::model::Vec3i;
using engine::world::model::World;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<Sample> samples() {
    std::vector<Sample> result;
    std::mt19937 random{813};
    for (unsigned index = 0; index < 900; ++index) {
        result.push_back({{static_cast<int>(random() % 32), static_cast<int>(random() % 32),
                           static_cast<int>(random() % 32)},
                          1});
    }
    return result;
}

double intersect(Vec3 origin, Vec3 direction, Vec3i voxel) {
    const std::array<int, 3> position{voxel.x, voxel.y, voxel.z};
    double near = 0;
    double far = 100000;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-14) {
            if (origin[axis] < position[axis] || origin[axis] >= position[axis] + 1) {
                return -1;
            }
            continue;
        }
        auto lower = (position[axis] - origin[axis]) / direction[axis];
        auto upper = (position[axis] + 1 - origin[axis]) / direction[axis];
        near = std::max(near, std::min(lower, upper));
        far = std::min(far, std::max(lower, upper));
    }
    return near <= far ? near : -1;
}

void test_trace(World& world, const std::vector<Sample>& points) {
    std::mt19937 random{765};
    for (unsigned index = 0; index < 1000; ++index) {
        const Vec3 origin{-10.5, (random() % 32000) / 1000.0, (random() % 32000) / 1000.0};
        const Vec3 target{32.5, (random() % 32000) / 1000.0, (random() % 32000) / 1000.0};
        const auto direction = engine::foundation::geometry::normalized(target - origin);
        double expected = 100000;
        for (const auto& point : points) {
            const auto distance = intersect(origin, direction, point.position);
            if (distance >= 0) {
                expected = std::min(expected, distance);
            }
        }
        const auto actual = world.trace(origin, direction);
        require(actual.hit == (expected != 100000), "AADF traversal missed/created hit");
        require(!actual.hit || std::abs(expected - actual.distance) < 1e-6,
                "AADF traversal hit is not nearest surface");
    }
}

void test_edits(World& world) {
    world.set({1, 1, 1}, 1);
    world.set({17, 1, 1}, 1);
    world.commit_edits();
    require(world.at({1, 1, 1}) == 1 && world.at({17, 1, 1}) == 1, "Edits lost data");
    world.set({1, 1, 1}, 0);
    world.commit_edits();
    require(world.at({1, 1, 1}) == 0 && world.at({17, 1, 1}) == 1,
            "Releasing deduplicated block modified another block");
    for (unsigned index = 0; index < 20; ++index) {
        world.set({1, 1, 1}, static_cast<std::uint16_t>(index & 1));
        world.commit_edits();
        require(world.at({1, 1, 1}) == (index & 1), "Reused block slot corrupt");
    }
}

void test_entities(World& world) {
    auto small = engine::world::model::build_model({2, 2, 2}, {{}, {}}, {{{0, 0, 0}, 1}});
    const auto model_index = world.add_model(std::move(small));
    const auto type = world.create_entity_type(model_index);
    const auto entity = world.spawn_entity(type, {20, 20, 20});
    world.update_entities(0.1, 1, 4);
    require(entity == 0 && !world.entity_gpu().instances.empty(), "Entity not indexed");
    require(world.entity_gpu().history[4 * 16384][0] != 0, "Entity history missing");
    world.clear_entities();
    require(world.entities().empty() && world.entity_gpu().instances.empty(),
            "Clear entity failed");
}

void test_materials(World& world) {
    const auto geometry_revision = world.view().geometry_revision;
    const auto material_revision = world.view().material_revision;
    engine::world::model::Material material;
    material.base = engine::world::model::MaterialBase::emissive;
    material.layer = engine::world::model::MaterialLayer::metallic_mirror;
    material.roughness = 0.5F;
    material.color_base = {1, 0.5, 0.25};
    material.color_layer = {100000, 0.00006103515625, 0.000000059604644775390625};
    const auto index = world.add_material(material);
    const std::array<std::uint32_t, 4> expected{0x3800000DU, 0x38003C00U, 0x7C003400U, 0x00010400U};
    require(world.view().materials[index] == expected,
            "IEEE half normal/subnormal/overflow or original material packing incorrect");
    require(world.view().geometry_revision == geometry_revision &&
                world.view().material_revision == material_revision + 1,
            "Material change needlessly invalidates immutable geometry buffers");
    material.color_base.x = 2;
    world.set_material(index, material);
    require(world.view().geometry_revision == geometry_revision &&
                world.view().material_revision == material_revision + 2,
            "Material property edit revision is incorrect");
}

void test_material_rename() {
    using engine::world::model::Material;
    World world;
    const auto first = world.add_material(Material{.name = "red", .color_base = {1, 0, 0}});
    const auto second = world.add_material(Material{.name = "green", .color_base = {0, 1, 0}});
    const auto revision = world.view().material_revision;
    auto renamed = world.materials()[second];
    renamed.name = world.materials()[first].name;
    bool rejected = false;
    try {
        world.set_material(second, renamed);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && world.materials()[second].name == "green" &&
                world.view().material_revision == revision,
            "Duplicate material rename modified the registry");
    renamed.name = "leaf";
    world.set_material(second, renamed);
    require(world.materials()[second].name == "leaf",
            "Renaming a material to an available name failed");
}

void test_generation() {
    auto small = engine::world::model::build_model({2, 2, 2}, {{}, {}}, {{{1, 1, 1}, 1}});
    World world;
    world.replace(std::move(small), {35, 35, 35}, 1);
    require(world.at({1, 1, 1}) == 1 && world.at({17, 1, 17}) == 1 && world.at({33, 1, 33}) == 1,
            "Model does not tile horizontally with chunk-aligned period");
    require(world.at({1, 17, 1}) == 0 && world.at({49, 1, 1}) == 0,
            "Model repeats vertically or extends beyond actual world dimensions");
    world.set({1, 1, 1}, 0);
    world.commit_edits();
    require(world.at({17, 1, 17}) == 1, "Tiled chunks share mutable block slots");
    auto full = engine::world::model::build_model({2, 2, 2}, {{}, {}}, {{{1, 1, 1}, 1}});
    world.replace(std::move(full), {17, 35, 35}, 1);
    require(world.at({17, 1, 1}) == 0, "Partial final world chunk was not clipped");
}

void test_library_persistence() {
    engine::world::model::Material red;
    red.name = "red";
    red.color_base = {1, 0, 0};
    engine::world::model::Material blue;
    blue.name = "blue";
    blue.color_base = {0, 0, 1};
    World world;
    world.replace(engine::world::model::build_model({4, 4, 4}, {{}, red}, {{{1, 1, 1}, 1}}), {}, 1);
    const auto imported =
        world.add_model(engine::world::model::build_model({2, 2, 2}, {{}, blue}, {{{0, 0, 0}, 1}}));
    const auto type = world.create_entity_type(imported);
    static_cast<void>(world.spawn_entity(type, {10, 10, 10}));
    red.color_base = {0, 1, 0};
    world.replace(engine::world::model::build_model({4, 4, 4}, {{}, red}, {{{2, 2, 2}, 1}}), {}, 1);
    const auto blue_id = engine::world::model::voxel_at(world.models()[imported], {});
    const auto red_id = world.at({2, 2, 2});
    require(world.models().size() == 1 && world.materials()[blue_id] == blue,
            "World replacement discarded the model library or its material");
    require(red_id != blue_id && world.materials()[red_id] == red,
            "World replacement did not use the new scene material");
    require(world.entity_types().empty() && world.entities().empty(),
            "World replacement preserved entities that belong to previous world");
    world.replace(world.models()[imported], {}, 1);
    require(world.at({0, 0, 0}) == blue_id && world.materials()[blue_id] == blue,
            "Replacing the scene with a registered model changed its material ID");
    const auto snapshot = world.snapshot();
    require(snapshot.materials.size() == 2 && snapshot.material_context == 0 &&
                engine::world::model::voxel_at(snapshot, {0, 0, 0}) == 1,
            "Saved world did not compact original used palette to local indices");
}

void test_scene_material_ids() {
    using namespace engine::world::model;
    World world;
    const auto green = world.add_material({"green", {0, 1, 0}});
    const std::vector<Sample> samples{{{0, 0, 0}, 1}, {{1, 1, 1}, 2}};
    auto source = build_model({2, 2, 2}, {{}, {"red", {1, 0, 0}}, {"green"}}, samples);
    std::vector<std::uint16_t> ids;
    const auto before = world.view().material_revision;
    auto mapped = world.prepare_generation(std::move(source), {}, &ids);
    require(ids.size() == 3 && ids[1] != green && ids[2] == green &&
                world.view().material_revision == before && !world.has_material(ids[1]),
            "Scene preparation did not retain library IDs or changed the active palette");
    const auto red = ids[1];
    world.replace(std::move(mapped), {}, 1);
    require(world.at(samples[0].position) == red && world.at(samples[1].position) == green &&
                world.has_material(red) && world.has_material(green) &&
                world.materials()[red].name == "red" && world.materials()[green].name == "green",
            "CPU replacement lost the prepared palette");

    const auto snapshot = world.snapshot(&ids);
    validate(snapshot);
    require(ids == std::vector<std::uint16_t>{0, green, red} &&
                ids[voxel_at(snapshot, samples[0].position)] == red &&
                ids[voxel_at(snapshot, samples[1].position)] == green,
            "Saved palette IDs did not match the compacted voxel indices");
}

void test_snapshot_shared_blocks() {
    using namespace engine::world::model;
    constexpr Vec3i first{1, 1, 1};
    constexpr Vec3i second{17, 1, 1};
    World world;
    world.replace(build_model({32, 16, 16}, {{}, {"stone"}, {"removed"}},
                              {{first, 1}, {second, 1}, {{24, 2, 2}, 2}}),
                  {}, 1);
    world.set({24, 2, 2}, 0);
    world.commit_edits();
    const auto snapshot = world.snapshot();
    validate(snapshot);
    require(snapshot.materials.size() == 2 && voxel_at(snapshot, first) == 1 &&
                voxel_at(snapshot, second) == 1,
            "Snapshot dropped a reachable voxel or retained an erased material");
    World reopened;
    reopened.replace(snapshot, {}, 1);
    reopened.set(first, 0);
    reopened.commit_edits();
    require(reopened.at(first) == 0 && reopened.at(second) == 1,
            "Editing a reopened chunk changed another chunk");
}

void test_palette_capacity() {
    using namespace engine::world::model;
    constexpr std::uint16_t last = 32767;
    std::vector<Material> palette(last + 1, {"stone", {1, 0, 0}});
    palette.front() = {};
    palette.back().color_base = {0, 0, 1};
    const auto source =
        build_model({16, 16, 16}, std::move(palette), {{{1, 1, 1}, 1}, {{2, 1, 1}, last}});
    World world;
    world.replace(source, {}, 1);
    require(world.materials().size() == last + 1 && world.at({1, 1, 1}) == 1 &&
                world.at({2, 1, 1}) == last && world.materials()[last].color_base == Vec3{0, 0, 1},
            "A full scene palette lost a material or consumed an extra ID");

    World library;
    const auto imported = library.add_model(source);
    const auto& model = library.models()[imported];
    require(voxel_at(model, {1, 1, 1}) == 1 && voxel_at(model, {2, 1, 1}) == last &&
                library.materials()[last].color_base == Vec3{0, 0, 1},
            "A full model palette merged entries or consumed an extra ID");
}

void test_legacy_zero() {
    using namespace engine::world::model;
    Model legacy;
    legacy.size = {16, 16, 16};
    legacy.materials = {{"stone", {1, 0, 0}}};
    legacy.chunks = {0x40000000U};
    World world;
    world.replace(legacy, {}, 1);
    const auto stone = world.at({1, 1, 1});
    require(stone != 0 && world.materials()[stone] == legacy.materials[0],
            "An occupied legacy local ID zero was mapped to empty space");
}

void test_edit_with_vacant_material_ids() {
    using namespace engine::world::model;
    World world;
    world.replace(build_model({16, 16, 16}, {{}, {"stone"}, {"grass"}}, {}), {}, 1);
    const auto retained = world.add_material({"library"});
    world.replace(build_model({16, 16, 16}, {{}, {"water"}}, {}), {}, 1);

    std::array<std::uint16_t, 4096> voxels{};
    voxels[0] = retained;
    voxels[1] = 2;
    require(!world.has_material(2) && world.has_material(retained),
            "Fixture requires a vacant ID below a retained one");
    bool rejected = false;
    try {
        world.set_chunk_voxels({}, voxels);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected && world.at({}) == 0, "Bulk edit accepted a vacant material ID");

    voxels[1] = 0;
    world.set_chunk_voxels({}, voxels);
    world.commit_edits();
    require(world.at({}) == retained, "Bulk edit rejected a registered ID above a vacant one");
    const auto reused = world.add_material({"paint"});
    require(reused == 2, "New material did not reuse the vacant ID");
    voxels[1] = reused;
    world.set_chunk_voxels({}, voxels);
    world.commit_edits();
    require(world.at({1, 0, 0}) == reused, "Bulk edit did not accept the reused material ID");
}

void test_stale_material_layout() {
    using namespace engine::world::model;
    World world;
    world.replace(build_model({16, 16, 16}, {{}, {"stone"}}, {{{1, 1, 1}, 1}}), {}, 1);
    const auto prepared =
        world.prepare_generation(build_model({16, 16, 16}, {{}, {"grass"}}, {{{2, 2, 2}, 1}}));
    const auto stone = world.at({1, 1, 1});
    const auto library = world.add_material({"library"});
    bool rejected = false;
    try {
        world.replace(prepared, {}, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && world.at({1, 1, 1}) == stone && world.at({2, 2, 2}) == 0 &&
                world.materials()[library].name == "library",
            "Stale preparation replaced the scene after a registry change");
}

void test_material_updates() {
    using namespace engine::world::model;
    World world;
    const auto first = world.add_material({"red", {1, 0, 0}});
    const auto second = world.add_material({"green", {0, 1, 0}});
    std::array<MaterialUpdate, 2> updates{
        {{first, {"green", {0, 0, 1}}}, {second, {"red", {1, 1, 0}}}}};
    const auto before = world.view();
    require(world.set_scene_materials(updates), "Valid palette did not update the registry");
    const auto after = world.view();
    require(after.material_revision == before.material_revision + 1 &&
                after.geometry_revision == before.geometry_revision &&
                after.chunk_revision == before.chunk_revision &&
                world.materials()[first] == updates[0].material &&
                world.materials()[second] == updates[1].material,
            "Palette update did not commit both entries without touching geometry");
    require(!world.set_scene_materials(updates), "Reapplying a palette changed the registry");

    const auto packed = after.materials[first];
    updates[0].material.color_base = {0, 1, 1};
    updates[1].material.roughness = std::numeric_limits<float>::infinity();
    bool rejected = false;
    try {
        static_cast<void>(world.set_scene_materials(updates));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected && world.materials()[first].color_base == Vec3{0, 0, 1} &&
                world.view().materials[first] == packed &&
                world.view().material_revision == after.material_revision,
            "Invalid palette left a partial update");
}

std::vector<unsigned> occupancy_prefix(const World& world, Vec3i dimensions) {
    const auto stride_x = static_cast<std::size_t>(dimensions.x + 1);
    const auto stride_y = stride_x * static_cast<std::size_t>(dimensions.y + 1);
    std::vector<unsigned> prefix(stride_y * static_cast<std::size_t>(dimensions.z + 1));
    const auto chunks = world.view().chunks;
    const auto& entities = world.entity_gpu().chunks;
    for (int z = 0; z < dimensions.z; ++z) {
        for (int y = 0; y < dimensions.y; ++y) {
            unsigned row = 0;
            for (int x = 0; x < dimensions.x; ++x) {
                const auto cell =
                    static_cast<std::size_t>(x + dimensions.x * (y + dimensions.y * z));
                row += (chunks[cell] >> 30) != 0 || entities[cell] != 0;
                const auto index = x + 1 + stride_x * (y + 1) + stride_y * (z + 1);
                prefix[index] = row + prefix[index - stride_x] + prefix[index - stride_y] -
                                prefix[index - stride_x - stride_y];
            }
        }
    }
    return prefix;
}

unsigned box_count(std::span<const unsigned> prefix, Vec3i dimensions, Vec3i lower, Vec3i upper) {
    const auto stride_x = static_cast<std::size_t>(dimensions.x + 1);
    const auto stride_y = stride_x * static_cast<std::size_t>(dimensions.y + 1);
    int result = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const auto x = (corner & 1) != 0 ? lower.x : upper.x;
        const auto y = (corner & 2) != 0 ? lower.y : upper.y;
        const auto z = (corner & 4) != 0 ? lower.z : upper.z;
        const auto value = prefix[x + stride_x * y + stride_y * z];
        const auto parity = (corner & 1U) ^ ((corner >> 1) & 1U) ^ ((corner >> 2) & 1U);
        result += parity == 0 ? static_cast<int>(value) : -static_cast<int>(value);
    }
    return static_cast<unsigned>(result);
}

void verify_bounds(const World& world) {
    const auto dimensions = engine::world::model::chunk_size(world.size());
    const auto prefix = occupancy_prefix(world, dimensions);
    const auto chunks = world.view().chunks;
    const auto& entities = world.entity_gpu().chunks;
    for (std::size_t index = 0; index < chunks.size(); ++index) {
        const auto word = chunks[index];
        if ((word >> 30) != 0) {
            continue;
        }
        if (entities[index] != 0) {
            require(word == 0, "Entity-occupied chunk retains unsafe empty extent");
            continue;
        }
        const Vec3i position{static_cast<int>(index % dimensions.x),
                             static_cast<int>(index / dimensions.x % dimensions.y),
                             static_cast<int>(index / dimensions.x / dimensions.y)};
        const Vec3i lower{position.x - static_cast<int>(word & 31U),
                          position.y - static_cast<int>((word >> 10) & 31U),
                          position.z - static_cast<int>((word >> 20) & 31U)};
        const Vec3i upper{position.x + static_cast<int>((word >> 5) & 31U) + 1,
                          position.y + static_cast<int>((word >> 15) & 31U) + 1,
                          position.z + static_cast<int>((word >> 25) & 31U) + 1};
        require(lower.x >= 0 && lower.y >= 0 && lower.z >= 0 && upper.x <= dimensions.x &&
                    upper.y <= dimensions.y && upper.z <= dimensions.z,
                "Directional chunk extent leaves world bounds");
        require(box_count(prefix, dimensions, lower, upper) == 0,
                "Directional empty extent crosses static or entity occupancy");
    }
}

void test_dynamic_bounds() {
    auto source = engine::world::model::build_model({16, 16, 16}, {{}, {}}, {});
    World world;
    world.replace(std::move(source), {2048, 64, 64}, 1);
    const auto far = world.view().chunks[100 + 2 * 128 + 2 * 128 * 4];
    const auto geometry_revision = world.view().revision;
    auto entity = engine::world::model::build_model({128, 1, 1}, {{}, {}}, {{{0, 0, 0}, 1}});
    const auto model_index = world.add_model(std::move(entity));
    const auto type = world.create_entity_type(model_index);
    static_cast<void>(world.spawn_entity(type, {128, 16, 16}));
    const auto stable_revision = world.view().revision;
    world.update_entities(0, 0, 0);
    verify_bounds(world);
    require(world.view().revision == stable_revision && stable_revision >= geometry_revision,
            "Entity animation changed static geometry revision");
    require(world.view().chunks[100 + 2 * 128 + 2 * 128 * 4] == far && far != 0,
            "Entity invalidated distant chunk directional fields");
    const auto& packed = world.entity_gpu().instances.front();
    require(((packed[4] >> 14) & 127U) == 0, "128-wide entity sentinel packing incorrect");
    const auto entity_revision = world.entity_gpu().revision;
    for (unsigned frame = 0; frame < 30; ++frame) {
        world.update_bounds(32768);
        verify_bounds(world);
    }
    world.update_entities(0, 0, 1);
    require(world.entity_gpu().revision == entity_revision,
            "Stationary entity redundantly uploads");
    world.set_entity_position(0, {512, 16, 16});
    world.update_entities(0.25, 1, 2);
    verify_bounds(world);
    world.update_bounds(37);
    verify_bounds(world);
    world.clear_entities();
    world.update_bounds(32768);
    verify_bounds(world);
}

} // namespace

int main() {
    try {
        const auto points = samples();
        auto model = engine::world::model::build_model({32, 32, 32}, {{}, {}}, points);
        World world;
        world.replace(std::move(model), {}, 1);
        for (const auto& point : points) {
            require(world.at(point.position) == 1,
                    "Packed model lost voxel during global registration");
        }
        test_trace(world, points);
        test_edits(world);
        test_entities(world);
        test_materials(world);
        test_material_rename();
        test_generation();
        test_library_persistence();
        test_scene_material_ids();
        test_snapshot_shared_blocks();
        test_palette_capacity();
        test_legacy_zero();
        test_edit_with_vacant_material_ids();
        test_stale_material_layout();
        test_material_updates();
        test_dynamic_bounds();
        std::cout << "World: 1000 independent nearest-hit comparisons; edits and entities passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
