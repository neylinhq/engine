#include <engine/world/model/world.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

using engine::world::model::GeneratedWorld;
using engine::world::model::IVec3;
using engine::world::model::Material;
using engine::world::model::Model;
using engine::world::model::World;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Material stone() {
    Material result;
    result.name = "stone";
    result.color_base = {0.5, 0.5, 0.5};
    return result;
}

GeneratedWorld generated(World& world) {
    Model model;
    model.size = {64, 64, 64};
    model.materials = {{}, stone()};
    model.chunks.resize(64);
    model.blocks.resize(192);
    model.voxels.resize(64);
    model.chunks[0] = 0x80000040U;
    model.chunks[1] = 0x80000080U;
    model.blocks[64] = 0x80000020U;
    model.blocks[128] = 0x80000020U;
    model.voxels[42] = 0x80010000U;
    return {world.prepare_generation(std::move(model)),
            {32, 16, 16},
            {{32, 2, 407000451U}},
            2,
            128,
            192};
}

void require_raw(const World& world) {
    const auto view = world.view();
    for (auto nodes : {view.chunks, view.blocks}) {
        for (auto word : nodes) {
            require((word >> 30) != 0 || word == 0, "CPU mirror acquired node AADF");
        }
    }
    for (auto word : view.voxels) {
        require((word & 0x8000U) != 0 || (word & 0xFFFFU) == 0,
                "CPU mirror acquired lower voxel AADF");
        require((word & 0x80000000U) != 0 || (word >> 16) == 0,
                "CPU mirror acquired upper voxel AADF");
    }
}

void require_unique(std::span<const std::uint32_t> indices) {
    require(std::ranges::is_sorted(indices), "GPU update destinations are not sorted");
    require(std::adjacent_find(indices.begin(), indices.end()) == indices.end(),
            "GPU update destinations contain a shader write race");
}

void require_journals_unique(const World& world) {
    const auto view = world.view();
    require_unique(view.changed_chunks);
    require_unique(view.changed_blocks);
    require_unique(view.changed_voxels);
    require_unique(view.invalidated_chunks);
    require_unique(world.entity_gpu().changed_chunks);
}

void test_adoption() {
    World world;
    world.adopt_generated(generated(world));
    require(world.view().gpu_authoritative, "Generated world did not select GPU authority");
    require(world.size() == IVec3{64, 64, 64} && world.actual_size() == IVec3{32, 16, 16},
            "Generation lost padded or requested dimensions");
    require(world.at({1, 1, 1}) == 2 && world.at({17, 1, 1}) == 2,
            "GPU mirror lost deduplicated source data");
    require(world.view().changed_chunks.empty(), "Adoption scheduled a full CPU upload");
    world.rebuild_bounds();
    world.update_bounds();
    require(world.view().invalidated_chunks.size() == 64,
            "Explicit rebuild did not schedule the GPU frontier");
    require_raw(world);
    world.acknowledge_changes();
    require(world.view().invalidated_chunks.empty(), "Acknowledged GPU frontier persists");
    const auto hit = world.trace({-1, 1.5, 1.5}, {1, 0, 0});
    require(hit.hit && hit.voxel == IVec3{1, 1, 1} && hit.distance == 2,
            "Raw pre-bounds query mirror cannot trace the nearest surface");
}

void remove_shared_voxel(World& world) {
    world.set({1, 1, 1}, 0);
    world.commit_edits();
    require(world.at({17, 1, 1}) == 2, "GPU hash reference release destroyed another chunk");
    require(world.view().changed_chunks.size() == 1 && world.view().changed_voxels.empty() &&
                world.view().changed_blocks.empty(),
            "Uniform edit emitted unnecessary block or voxel allocation records");
    world.acknowledge_changes();
    world.set({18, 1, 1}, 2);
    world.commit_edits();
    auto view = world.view();
    require(view.changed_voxels.size() == 1 && view.changed_voxels[0] == 64,
            "New mixed voxel block was not journaled");
    require(view.changed_blocks.size() == 1 && view.changed_blocks[0] == 128 &&
                (view.chunks[1] & 0x3FFFFFFFU) == 128,
            "Original SetBlocks in-place mixed-chunk update was not preserved");
    require(view.invalidated_chunks.empty(),
            "Occupied-to-occupied edit needlessly invalidated original chunk AADF");
    world.acknowledge_changes();
}

void test_edits() {
    World world;
    world.adopt_generated(generated(world));
    remove_shared_voxel(world);
    world.set({1, 1, 1}, 2);
    world.set({2, 1, 1}, 2);
    world.commit_edits();
    auto view = world.view();
    require(view.changed_voxels.empty(), "CPU edit hash failed to reuse a shared voxel block");
    require(view.changed_blocks.size() == 1 && view.changed_blocks[0] == 64,
            "Original FIFO freed chunk-block slot was not reused");
    world.set({3, 1, 1}, 2);
    world.commit_edits();
    require(world.view().changed_voxels.size() == 1 && world.view().changed_voxels[0] == 32,
            "Original FIFO freed voxel slot was not reused");
    require(world.at({19, 1, 1}) == 0 && world.at({3, 1, 1}) == 2,
            "Edit of shared data changed another chunk");
    require_journals_unique(world);
    require_raw(world);
    world.acknowledge_changes();
    view = world.view();
    require(view.changed_chunks.empty() && view.changed_blocks.empty() &&
                view.changed_voxels.empty() && view.invalidated_chunks.empty(),
            "Submitted GPU edits were not acknowledged");
}

void test_bulk_edits() {
    World world;
    world.adopt_generated(generated(world));
    world.fill_chunk({0, 0, 0}, 2);
    require(world.at({0, 0, 0}) == 2 && world.at({15, 15, 15}) == 2,
            "Pending uniform chunk is not visible to voxel queries");
    world.set({4, 5, 6}, 0);
    auto dense = world.chunk_voxels({0, 0, 0});
    require(dense[4 + 5 * 16 + 6 * 256] == 0 && dense[0] == 2,
            "Voxel edit did not preserve the pending uniform chunk");
    dense[1] = 0;
    world.set_chunk_voxels({0, 0, 0}, dense);
    world.commit_edits();
    require(world.at({1, 0, 0}) == 0 && world.at({0, 0, 0}) == 2 && world.at({17, 1, 1}) == 2,
            "Bulk update lost cells or damaged another shared chunk");
    world.fill_chunk({0, 0, 0}, 0);
    world.commit_edits();
    require(world.at({0, 0, 0}) == 0 && world.at({17, 1, 1}) == 2,
            "Uniform erase released a voxel block still used by another chunk");
    require_journals_unique(world);
    require_raw(world);
    world.acknowledge_changes();
    const auto revision = world.view().revision;
    world.fill_chunk({0, 0, 0}, 0);
    world.commit_edits();
    require(world.view().revision == revision, "Unchanged uniform fill created a new edit");
}

void test_entities() {
    World world;
    world.adopt_generated(generated(world));
    const auto source =
        engine::world::model::build_model({2, 2, 2}, {{}, stone()}, {{{0, 0, 0}, 1}});
    const auto index = world.add_model(source);
    const auto type = world.create_entity_type(index);
    static_cast<void>(world.spawn_entity(type, {20, 20, 20}));
    world.update_entities(0, 0, 3);
    require(world.entity_gpu().entity_count == 1 && !world.entity_gpu().changed_chunks.empty(),
            "Entity count or compact membership journal is missing");
    require(!world.view().invalidated_chunks.empty(), "Entity arrival did not invalidate AADF");
    require(world.entity_gpu().history[3 * 16384][0] != 0, "Entity history record is missing");
    world.acknowledge_changes();
    world.update_entities(0, 0, 4);
    require(world.entity_gpu().changed_chunks.empty() && world.view().invalidated_chunks.empty(),
            "Stationary entity generated redundant chunk changes");
    world.set_entity_position(0, {40, 20, 20});
    world.update_entities(0, 0, 5);
    require(!world.entity_gpu().changed_chunks.empty(), "Entity motion did not update membership");
    world.clear_entities();
    require(world.entity_gpu().entity_count == 0 && world.entity_gpu().instances.empty(),
            "Cleared entities retain live GPU instance count");
    require(std::ranges::all_of(world.entity_gpu().chunks, [](auto chunk) { return chunk == 0; }),
            "Cleared entity chunks retain GPU membership");
    require_journals_unique(world);
    require_raw(world);
    world.adopt_generated(generated(world));
    require(world.models().size() == 1 && world.entity_types().empty(),
            "GPU replacement lost model library or retained old entity types");
}

void test_add_before_release() {
    World world;
    auto data = generated(world);
    data.model.chunks[1] = 0;
    data.hash.front()[1] = 1;
    data.model.blocks[65] = 0x80000040U;
    data.model.voxels.resize(96);
    data.model.voxels[64] = 0x8002U;
    data.hash.push_back({64, 1, 1250000831U});
    data.logical_voxels = 192;
    data.compression = 1;
    world.adopt_generated(std::move(data));
    world.set({1, 1, 1}, 0);
    world.set({5, 0, 0}, 2);
    world.commit_edits();
    const auto changed = world.view().changed_voxels;
    require(changed.size() == 1 && changed[0] == 96,
            "Old voxel references were freed before all new chunk blocks were acquired");
    require(world.at({4, 0, 0}) == 2 && world.at({5, 0, 0}) == 2,
            "Add-before-release edit ordering lost voxel data");
}

void test_invalid_adoption() {
    World world;
    world.adopt_generated(generated(world));
    auto invalid = generated(world);
    invalid.block_words = 64;
    bool rejected = false;
    try {
        world.adopt_generated(std::move(invalid));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected && world.at({17, 1, 1}) == 2,
            "Invalid GPU metadata replaced the valid query mirror");
}

} // namespace

int main() {
    try {
        test_adoption();
        test_edits();
        test_bulk_edits();
        test_entities();
        test_add_before_release();
        test_invalid_adoption();
        std::cout
            << "GPU mirror: raw adoption, query, edit journals, hash reuse and entities passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
