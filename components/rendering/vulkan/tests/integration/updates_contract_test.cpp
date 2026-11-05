#include "../support/bounds_oracle.hpp"
#include "../support/chunks/layout.hpp"
#include "../support/resident_rays.hpp"

#include "../../src/world_updates/support.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>

namespace {
namespace model = engine::world::model;
namespace renderer = engine::rendering::vulkan;
namespace test = engine::rendering::vulkan::test;
using test::require_update;

std::size_t index_of(model::IVec3 point, model::IVec3 size) {
    return point.x + static_cast<std::size_t>(point.y) * size.x +
           static_cast<std::size_t>(point.z) * size.x * size.y;
}

std::uint16_t material_index(const model::World& world, const std::string& name) {
    const auto found = std::ranges::find(world.materials(), name, &model::Material::name);
    require_update(found != world.materials().end(), "Fixture material missing");
    return static_cast<std::uint16_t>(found - world.materials().begin());
}

model::Model empty_model() {
    std::vector<model::Material> materials(3);
    materials[1].name = "update_first";
    materials[2].name = "update_second";
    return model::build_model({64, 64, 64}, materials, {});
}

void verify_geometry(test::UpdateProbe& probe, std::span<const std::uint16_t> dense) {
    const auto gpu = probe.snapshot();
    for (std::size_t index = 0; index < dense.size(); ++index) {
        const model::IVec3 point{static_cast<int>(index % gpu.size.x),
                                 static_cast<int>((index / gpu.size.x) % gpu.size.y),
                                 static_cast<int>(index / gpu.size.x / gpu.size.y)};
        require_update(model::voxel_at(gpu, point) == dense[index],
                       "GPU mutation geometry differs from dense edit oracle");
    }
    test::verify_local_bounds(test::update_mirror(probe.world), gpu);
    const auto chunks = test::update_chunks(probe.compute);
    test::verify_chunk_occupancy(probe.world, chunks);
    test::verify_chunk_bounds(model::chunk_size(gpu.size), chunks);
}

void write_voxel(test::UpdateProbe& probe, std::vector<std::uint16_t>& dense,
                 model::Sample sample) {
    probe.world.set(sample.position, sample.material);
    dense[index_of(sample.position, probe.world.size())] = sample.material;
}

void write_box(test::UpdateProbe& probe, std::vector<std::uint16_t>& dense, model::IVec3 origin,
               model::IVec3 size) {
    const auto material = material_index(probe.world, "update_second");
    const bool uniform = size == model::IVec3{16, 16, 16};
    if (uniform) {
        probe.world.fill_chunk({origin.x / 16, origin.y / 16, origin.z / 16}, material);
    }
    for (int index = 0; index < size.x * size.y * size.z; ++index) {
        const model::IVec3 point{origin.x + index % size.x, origin.y + (index / size.x) % size.y,
                                 origin.z + index / size.x / size.y};
        if (uniform) {
            dense[index_of(point, probe.world.size())] = material;
        } else {
            write_voxel(probe, dense, {point, material});
        }
    }
}

void verify_vacuum(test::UpdateProbe& probe) {
    probe.settle();
    test::verify_queue_empty(probe.compute);
    const auto before = test::update_chunks(probe.compute);
    require_update(
        std::ranges::all_of(
            before, [](const auto& item) { return item[0] == 0x3FFFFFFFU && item[1] == 0; }),
        "GPU AADF did not fully expand a vacuum");
    probe.settle();
    test::verify_queue_empty(probe.compute);
    require_update(before == test::update_chunks(probe.compute),
                   "Empty AADF queues resurrected stale work");
}

void editing_contract(test::UpdateProbe& probe, test::ResidentRays& rays) {
    probe.load(empty_model(), {128, 64, 128});
    verify_vacuum(probe);
    const auto size = probe.world.size();
    std::vector<std::uint16_t> dense(static_cast<std::size_t>(size.x) * size.y * size.z);
    const auto material = material_index(probe.world, "update_first");
    write_voxel(probe, dense, {{17, 21, 19}, material});
    write_voxel(probe, dense, {{81, 21, 83}, material});
    write_box(probe, dense, {28, 24, 28}, {4, 4, 4});
    probe.world.commit_edits();
    const auto changes = probe.world.view();
    require_update(!changes.changed_chunks.empty() && !changes.changed_blocks.empty() &&
                       !changes.changed_voxels.empty() && !changes.invalidated_chunks.empty(),
                   "Real editing did not produce every original GPU change record");
    probe.flush(127, 1);
    verify_geometry(probe, dense);
    rays.verify_static();
    probe.settle(1, 160);
    test::verify_queue_empty(probe.compute);
    verify_geometry(probe, dense);
    rays.verify_static();
    write_voxel(probe, dense, {{17, 21, 19}, 0});
    write_box(probe, dense, {64, 16, 64}, {16, 16, 16});
    probe.world.commit_edits();
    probe.flush(126);
    verify_geometry(probe, dense);
    rays.verify_static();
    probe.settle();
    test::verify_queue_empty(probe.compute);
    verify_geometry(probe, dense);
    rays.verify_static();
    std::cout << "Four GPU edit states matched " << dense.size()
              << " voxels each and independent block/voxel/chunk AADF boxes\n";
}

void verify_entity_buffers(test::UpdateProbe& probe, unsigned history_index) {
    const auto& data = probe.world.entity_gpu();
    const auto instances =
        probe.compute.read("entityChunkInstances", 0, data.instances.size() * 20);
    const auto expected_instances = std::as_bytes(std::span(data.instances));
    require_update(std::ranges::equal(instances, expected_instances),
                   "GPU entity instance copy differs from original packed records");
    const auto bytes = static_cast<std::size_t>(data.entity_count) * 16;
    const auto history =
        probe.compute.read("entityInstancesHistory", history_index * 16384U * 16U, bytes);
    const auto expected_history =
        std::as_bytes(std::span(data.history).subspan(history_index * 16384U, data.entity_count));
    require_update(std::ranges::equal(history, expected_history),
                   "GPU entity history copy wrote the wrong TAA row or count");
    const auto chunks = test::update_chunks(probe.compute);
    test::verify_chunk_occupancy(probe.world, chunks);
    test::verify_chunk_bounds(model::chunk_size(probe.world.size()), chunks);
}

void append_entity_type(test::UpdateProbe& probe) {
    const auto previous = probe.world.entity_gpu().voxels.size();
    std::vector<model::Material> materials(2);
    materials[1].name = "appended_entity";
    const auto source = model::build_model({1, 1, 1}, materials, {{{0, 0, 0}, 1}});
    const auto model_index = probe.world.add_model(source);
    static_cast<void>(probe.world.create_entity_type(model_index));
    probe.world.update_entities(0, 0, 125);
    const auto& data = probe.world.entity_gpu();
    require_update(data.voxels.size() > previous, "Entity type did not append voxel storage");
    const auto bytes = (data.voxels.size() - previous) * 4U + data.entity_count * 16U;
    probe.flush(125);
    require_update(probe.updates.uploaded_entity_bytes() == bytes,
                   "Entity append uploaded an existing immutable voxel prefix");
    const auto gpu = test::update_words(probe.compute, "entityVoxelData", data.voxels.size());
    require_update(gpu == data.voxels, "Entity voxel buffer growth lost an existing type");
}

void verify_occupied_chunk(test::UpdateProbe& probe, std::size_t index, std::uint32_t geometry,
                           bool has_entities) {
    const auto chunk = test::update_chunks(probe.compute).at(index);
    require_update(chunk[0] == geometry, "Entity chunk update overwrote occupied geometry");
    require_update((chunk[1] != 0) == has_entities,
                   "Entity chunk update did not change the expected chunk list");
}

std::size_t preserve_geometry_during_entity_changes(test::UpdateProbe& probe, std::size_t type,
                                                    std::size_t entity, std::size_t chunk_index) {
    const auto geometry = test::update_chunks(probe.compute).at(chunk_index)[0];
    const std::array<model::Vec3, 2> positions{{{105, 40, 100}, {30, 40, 40}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        probe.world.set_entity_position(entity, positions[index]);
        probe.world.update_entities(0, 0, 125);
        probe.flush(125);
        verify_occupied_chunk(probe, chunk_index, geometry, index == 1);
    }
    probe.world.clear_entities();
    probe.flush(125);
    verify_occupied_chunk(probe, chunk_index, geometry, false);
    const auto replacement = probe.world.spawn_entity(type, positions.back());
    probe.world.update_entities(0, 0, 125);
    probe.flush(125);
    verify_occupied_chunk(probe, chunk_index, geometry, true);
    return replacement;
}

std::size_t shared_chunk_edit_contract(test::UpdateProbe& probe, std::size_t type,
                                       std::size_t entity) {
    const model::IVec3 point{31, 47, 47};
    const auto index =
        index_of({point.x / 16, point.y / 16, point.z / 16}, model::chunk_size(probe.world.size()));
    const auto before = test::update_chunks(probe.compute).at(index);
    require_update(before[1] != 0 && probe.world.at(point) == 0,
                   "Shared chunk fixture must have an entity and an empty static voxel");
    const auto material = material_index(probe.world, "update_first");
    probe.world.set(point, material);
    probe.world.commit_edits();
    probe.flush(125);
    const auto occupied = test::update_chunks(probe.compute).at(index);
    require_update(occupied[1] == before[1], "Geometry insertion overwrote an entity chunk list");
    require_update(model::voxel_at(probe.snapshot(), point) == material,
                   "Shared chunk geometry insertion did not reach GPU storage");
    const auto replacement = preserve_geometry_during_entity_changes(probe, type, entity, index);
    const auto entities = test::update_chunks(probe.compute).at(index)[1];
    probe.world.set(point, 0);
    probe.world.commit_edits();
    probe.flush(125);
    require_update(test::update_chunks(probe.compute).at(index)[1] == entities,
                   "Geometry removal overwrote an entity chunk list");
    require_update(model::voxel_at(probe.snapshot(), point) == 0,
                   "Shared chunk geometry removal did not reach GPU storage");
    std::cout << "Shared chunk geometry and entity updates preserved each other's packed word\n";
    return replacement;
}

void entity_contract(test::UpdateProbe& probe, test::ResidentRays& rays) {
    std::vector<model::Sample> samples;
    for (int index = 0; index < 64; ++index) {
        samples.push_back({{index % 4, (index / 4) % 4, index / 16}, 1});
    }
    std::vector<model::Material> materials(2);
    materials[1].name = "resident_entity";
    const auto source = model::build_model({4, 4, 4}, materials, samples);
    const auto model_index = probe.world.add_model(source);
    const auto type = probe.world.create_entity_type(model_index);
    const auto material = material_index(probe.world, "resident_entity");
    auto entity = probe.world.spawn_entity(type, {30, 40, 40});
    probe.world.update_entities(0, 0, 125);
    probe.flush(125);
    verify_entity_buffers(probe, 125);
    entity = shared_chunk_edit_contract(probe, type, entity);
    append_entity_type(probe);
    rays.verify_entity(model::Vec3{30, 40, 40}, material);
    probe.settle();
    verify_entity_buffers(probe, 125);
    probe.world.set_entity_position(entity, {105, 40, 100});
    probe.world.update_entities(0, 0, 124);
    probe.flush(124);
    verify_entity_buffers(probe, 124);
    rays.verify_entity(model::Vec3{105, 40, 100}, material);
    probe.settle();
    verify_entity_buffers(probe, 124);
    probe.world.clear_entities();
    probe.flush(123);
    const auto chunks = test::update_chunks(probe.compute);
    test::verify_chunk_occupancy(probe.world, chunks);
    test::verify_chunk_bounds(model::chunk_size(probe.world.size()), chunks);
    require_update(std::ranges::all_of(chunks, [](const auto& item) { return item[1] == 0; }),
                   "Entity removal left a stale GPU chunk list");
    rays.verify_entity({}, material);
    std::cout << "GPU entity insertion, movement, history rows, and removal matched CPU records\n";
}

void prepare_empty_chunks(test::UpdateProbe& probe, vk::Extent3D extent) {
    probe.chunks = renderer::make_chunk_images(probe.device, extent);
    const auto command = probe.compute.command();
    for (const auto& image : {probe.chunks.geometry, probe.chunks.entities}) {
        if (!image) {
            continue;
        }
        renderer::image_barrier(command, *image->handle, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eGeneral);
        command.clearColorImage(
            *image->handle, vk::ImageLayout::eGeneral,
            vk::ClearColorValue(std::array<std::uint32_t, 4>{}),
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
    }
    renderer::memory_barrier(command);
}

void partial_group_contract(test::UpdateProbe& probe) {
    probe.compute.synchronize();
    prepare_empty_chunks(probe, {5, 3, 2});
    probe.updates.initialize({5, 3, 2});
    probe.settle(1, 40);
    test::verify_queue_empty(probe.compute);
    const auto chunks = test::update_chunks(probe.compute);
    require_update(
        std::ranges::all_of(chunks, [](const auto& item) { return item[0] == 0x3FFFFFFFU; }),
        "Partial AADF workgroup failed to expand active chunks");
    const std::array<std::uint32_t, 1> changed{29};
    renderer::FrameInput input;
    input.world.invalidated_chunks = changed;
    probe.updates.apply(input);
    probe.settle(1, 40);
    require_update(chunks == test::update_chunks(probe.compute),
                   "Partial invalidation workgroup changed vacuum geometry");
    std::cout << "Partial queue/workgroup lanes and empty-dispatch guards passed\n";
}

void wide_dispatch_contract(test::UpdateProbe& probe) {
    constexpr std::uint32_t count = 32769;
    std::vector<std::uint32_t> voxels(count * 32U);
    std::vector<std::uint32_t> offsets(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        offsets[index] = index * 32U;
        voxels[offsets[index]] = 0xFFFFU;
    }
    renderer::FrameInput input;
    input.world.voxels = voxels;
    input.world.changed_voxels = offsets;
    probe.updates.apply(input);
    const auto actual = test::update_words(probe.compute, "voxels", voxels.size());
    for (const auto offset : offsets) {
        require_update((actual[offset] & 0xFFFFU) == 0xFFFFU,
                       "Wide voxel scatter changed maximum material or skipped a row");
        require_update(actual[offset + 31] != 0, "Wide voxel scatter did not compute AADF");
    }
    std::array<renderer::GroupRecord, 96> queues{};
    queues.front() = {17, 70000};
    probe.compute.upload("boundQueueInfo", std::as_bytes(std::span(queues)));
    renderer::UniformValues values;
    renderer::set_value(values, "boundGroupQueueMaxSize", 70000U);
    renderer::set_value(values, "maxGroupBoundDispatch", 70000U);
    probe.compute.run("world_data_boundsCalc_prepareGroupBounds", {1, 1, 1}, values);
    const auto dispatch = test::update_words(probe.compute, "boundGroupQueueDispatchCount", 3);
    require_update(dispatch == std::vector<std::uint32_t>{32768, 3, 1},
                   "Large AADF budget exceeds Vulkan dispatch limits");
    std::cout << "32769 voxel-record dispatch and 70000-group indirect preparation passed\n";
}

void verify_initial_queue_planes(renderer::WorldCompute& compute, model::IVec3 groups) {
    const auto count = static_cast<std::size_t>(groups.x) * groups.y * groups.z;
    const auto queues = test::update_words(compute, "boundGroupQueues", count * 3);
    const auto masks = test::update_words(compute, "boundGroupMasks", count * 3);
    for (std::size_t index = 0; index < count; ++index) {
        const auto x = static_cast<std::uint32_t>(index % groups.x);
        const auto y = static_cast<std::uint32_t>((index / groups.x) % groups.y);
        const auto z = static_cast<std::uint32_t>(index / groups.x / groups.y);
        const auto packed = x | (y << 11U) | (z << 21U);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            require_update(queues[axis * count + index] == packed,
                           "Large initial queue skipped or mispacked a group coordinate");
            require_update(masks[index * 3 + axis] == 1,
                           "Large initial queue did not initialize every group axis mask");
        }
    }
}

void large_initial_queue_contract(renderer::Device& device, const std::filesystem::path& shaders) {
    // This covers the exact initializer workload for 16384x4096x16384 voxels.
    // It is kernel coverage, not a full-world rendering or performance test:
    // only the three initial queue planes and masks are allocated (48 MiB each).
    // No chunk images, later queue levels, world generation or limit override is used.
    constexpr model::IVec3 groups{256, 64, 256};
    constexpr std::uint32_t count = 256U * 64U * 256U;
    constexpr vk::DeviceSize bytes = vk::DeviceSize{count} * 3 * sizeof(std::uint32_t);
    renderer::BufferMap buffers;
    renderer::ChunkImages chunks;
    renderer::WorldCompute compute(device, shaders, buffers, chunks);
    compute.allocate("boundGroupQueues", bytes);
    compute.allocate("boundGroupMasks", bytes);
    renderer::UniformValues values;
    renderer::set_value(values, "groupSizeX", groups.x);
    renderer::set_value(values, "groupSizeY", groups.y);
    renderer::set_value(values, "groupSizeZ", groups.z);
    renderer::set_value(values, "boundsInitOffset", 0U);
    renderer::set_value(values, "boundGroupQueueMaxSize", count);
    const auto dispatch = renderer::update_grid((count + 63U) / 64U);
    require_update(dispatch == vk::Extent3D{32768, 2, 1},
                   "Large initial queue did not partition its 65536 workgroups into two rows");
    compute.run("world_data_boundsCalc_addInitialGroupsToBoundQueue", dispatch, values);
    verify_initial_queue_planes(compute, groups);
    require_update(!chunks.geometry && !chunks.entities && buffers.size() == 2,
                   "Initializer coverage unexpectedly allocated a world or extra queue levels");
    require_update(device.validation_errors.load() == 0,
                   "Large initial queue Vulkan validation reported errors");
    std::cout << "Initializer-only coverage: all 4194304 coordinates in three queue planes and "
                 "all axis masks matched; both dispatch rows, their boundary and final entries "
                 "were checked. This is not a 16384x4096x16384 full-world benchmark.\n";
}

void verify_execution(const test::UpdateProbe& probe) {
    for (const auto* name :
         {"world_data_worldChange_applyChunkChange", "world_data_worldChange_applyBlockChange",
          "world_data_worldChange_applyVoxelChange", "world_data_worldChange_applyGroupChange",
          "world_data_boundsCalc_addInitialGroupsToBoundQueue",
          "world_data_boundsCalc_prepareGroupBounds", "world_data_boundsCalc_computeGroupBounds",
          "world_data_entityUpdate_updateChunks",
          "world_data_entityUpdate_copyEntityChunkInstances",
          "world_data_entityUpdate_copyEntityHistory"}) {
        require_update(probe.compute.execution_counts().contains(name),
                       "Original mutation/AADF/entity program was never dispatched");
    }
    require_update(probe.device.validation_errors.load() == 0,
                   "GPU mutation Vulkan validation reported errors");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require_update(argc == 3 || argc == 4,
                       "Usage: rendering_mutation_test SHADER_DIRECTORY RAY_DIRECTORY "
                       "[--packed|--split]");
        const auto layout =
            argc == 4 ? test::parse_chunk_layout(argv[3]) : renderer::ChunkLayout::automatic;
        test::UpdateProbe probe(argv[1], layout);
        large_initial_queue_contract(probe.device, argv[1]);
        test::ResidentRays rays(probe, argv[2]);
        editing_contract(probe, rays);
        entity_contract(probe, rays);
        partial_group_contract(probe);
        wide_dispatch_contract(probe);
        verify_execution(probe);
        std::cout << "All original GPU mutation, AADF, and entity contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
