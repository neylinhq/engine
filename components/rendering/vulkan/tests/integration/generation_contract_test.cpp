#include "../../src/world/compute.hpp"
#include "../../src/world/generation.hpp"
#include "../../src/world/updates.hpp"
#include "../support/chunks/layout.hpp"

#include <engine/world/storage/storage.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace model = engine::world::model;
namespace renderer = engine::rendering::vulkan;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

engine::platform::window::Window create_window() {
    auto result = engine::platform::window::Window::create({32, 32, "GPU generation", false});
    if (!result) {
        throw std::runtime_error(result.error());
    }
    return std::move(*result);
}

std::uint16_t expected_at(const model::Model& source, model::IVec3 requested, model::IVec3 point) {
    if (point.x >= requested.x || point.y >= requested.y || point.z >= requested.z ||
        point.y >= model::chunk_size(source.size).y * 16) {
        return 0;
    }
    point.x %= model::chunk_size(source.size).x * 16;
    point.z %= model::chunk_size(source.size).z * 16;
    return model::voxel_at(source, point);
}

void compare_voxels(const model::Model& source, const renderer::GenerationResult& result) {
    const auto size = result.model.size;
    const auto count = std::uint64_t(size.x) * size.y * size.z;
    for (std::uint64_t index = 0; index < count; ++index) {
        const model::IVec3 point{static_cast<int>(index % size.x),
                                 static_cast<int>((index / size.x) % size.y),
                                 static_cast<int>(index / (size.x * size.y))};
        const auto expected = expected_at(source, result.actual_size, point);
        if (model::voxel_at(result.model, point) != expected) {
            std::cerr << "Voxel mismatch " << point.x << ',' << point.y << ',' << point.z
                      << " expected " << expected << '\n';
            throw std::runtime_error("GPU generation differs from source tiling oracle");
        }
    }
    std::cout << count << " generated voxels matched source/clip/padding oracle\n";
}

void compare_hash(const renderer::GenerationResult& result) {
    std::map<std::uint32_t, std::uint32_t> references;
    for (const auto block : result.model.blocks) {
        if ((block >> 30) == 2) {
            ++references[block & 0x3FFFFFFFU];
        }
    }
    std::set<std::array<std::uint32_t, 32>> unique;
    for (const auto& entry : result.hash) {
        if (entry[0] == 0) {
            continue;
        }
        require(references.at(entry[0]) == entry[1], "GPU hash use count differs from graph");
        std::array<std::uint32_t, 32> words{};
        std::copy_n(result.model.voxels.begin() + entry[0], 32, words.begin());
        require(unique.insert(words).second, "GPU dedup allocated duplicate blocks");
        std::uint32_t hash = 1;
        for (const auto word : words) {
            hash = hash * 31U + (word & 0x7FFFU);
            hash = hash * 31U + ((word >> 16) & 0x7FFFU);
        }
        require(hash == entry[2], "GPU rolling hash differs from independent scalar oracle");
    }
    require(unique.size() == references.size(), "GPU hash misses a referenced block");
}

std::vector<std::uint32_t> read_words(renderer::WorldCompute& compute, const char* name,
                                      std::size_t count) {
    const auto data = compute.read(name, 0, count * 4);
    std::vector<std::uint32_t> result(count);
    std::memcpy(result.data(), data.data(), data.size());
    return result;
}

void check_cell_box(std::span<const std::uint32_t, 64> before, std::uint32_t bound,
                    std::uint32_t index, unsigned shift) {
    const std::array<int, 3> point{static_cast<int>(index % 4), static_cast<int>((index / 4) % 4),
                                   static_cast<int>(index / 16)};
    std::array<int, 3> low{}, high{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        low[axis] = point[axis] - static_cast<int>((bound >> (axis * 4)) & 3U);
        high[axis] = point[axis] + static_cast<int>((bound >> (axis * 4 + 2)) & 3U);
        require(low[axis] >= 0 && high[axis] < 4, "Local AADF crosses a block boundary");
    }
    for (int z = low[2]; z <= high[2]; ++z) {
        for (int y = low[1]; y <= high[1]; ++y) {
            for (int x = low[0]; x <= high[0]; ++x) {
                require((before[x + y * 4 + z * 16] >> shift) == 0,
                        "GPU local AADF skips an occupied cell");
            }
        }
    }
}

void check_bounds(std::span<const std::uint32_t, 64> before,
                  std::span<const std::uint32_t, 64> after, unsigned shift) {
    for (std::uint32_t index = 0; index < 64; ++index) {
        if ((before[index] >> shift) != 0) {
            require(before[index] == after[index], "GPU local AADF changed occupied data");
        } else {
            require(before[index] == 0, "CPU mirror unexpectedly contains generated AADF");
            require((after[index] >> shift) == 0, "GPU local AADF changed occupancy");
            check_cell_box(before, after[index], index, shift);
        }
    }
}

void compare_bounds(renderer::WorldCompute& compute, const renderer::GenerationResult& result) {
    const auto blocks = read_words(compute, "blocks", result.model.blocks.size());
    const auto voxels = read_words(compute, "voxels", result.model.voxels.size());
    for (std::size_t offset = 0; offset < blocks.size(); offset += 64) {
        check_bounds(std::span<const std::uint32_t, 64>(result.model.blocks.data() + offset, 64),
                     std::span<const std::uint32_t, 64>(blocks.data() + offset, 64), 30);
    }
    for (std::size_t offset = 0; offset < voxels.size(); offset += 32) {
        std::array<std::uint32_t, 64> before{}, after{};
        for (std::size_t index = 0; index < 64; ++index) {
            const auto shift = static_cast<unsigned>(index % 2) * 16;
            before[index] = (result.model.voxels[offset + index / 2] >> shift) & 0xFFFFU;
            after[index] = (voxels[offset + index / 2] >> shift) & 0xFFFFU;
        }
        check_bounds(before, after, 15);
    }
    require(blocks.front() != 0 && voxels.front() != 0,
            "GPU local AADF programs did not populate reserved empty ranges");
}

std::uint16_t source_material(int pattern, model::IVec3 point) {
    if (pattern == 0) {
        return 0;
    }
    if (pattern == 1) {
        return 1;
    }
    if (pattern == 2) {
        return point.x % 2 == 0 ? 1 : 0;
    }
    const auto block = (point.x / 4) + (point.y / 4) * 16 + (point.z / 4) * 256;
    const auto local = point.x % 4 + (point.y % 4) * 4 + (point.z % 4) * 16;
    if (local == 63) {
        return 0;
    }
    return local < 12 ? static_cast<std::uint16_t>((block >> local) & 1) : 1;
}

model::Model source_model(int pattern) {
    const int side = pattern == 3 ? 64 : 16;
    std::vector<model::Sample> samples;
    for (int index = 0; index < side * side * side; ++index) {
        const model::IVec3 point{index % side, (index / side) % side, index / (side * side)};
        const auto material = source_material(pattern, point);
        if (material != 0) {
            samples.push_back({point, material});
        }
    }
    return model::build_model({side, side, side}, std::vector<model::Material>(2), samples);
}

struct QueueCapacityCase {
    model::IVec3 requested;
    model::IVec3 chunks;
    std::uint64_t bytes;
};

QueueCapacityCase unsupported_queue(const renderer::Device& device) {
    constexpr std::uint64_t reported_bytes = 1610612736;
    if (device.properties.limits.maxStorageBufferRange < reported_bytes) {
        return {{16129, 3841, 16129}, {1024, 256, 1024}, reported_bytes};
    }
    // A 6 GiB queue exceeds the real uint32 Vulkan limit without changing cached capabilities.
    return {{16129, 16129, 16129}, {1024, 1024, 1024}, 6442450944ULL};
}

void reject_queue_capacity(renderer::WorldCompute& compute, const model::Model& source,
                           const QueueCapacityCase& target, bool initialize_updates) {
    std::string failure;
    try {
        if (initialize_updates) {
            renderer::WorldUpdates updates(compute);
            updates.initialize(target.chunks);
        } else {
            static_cast<void>(renderer::generate_world(compute, source, target.requested, 4));
        }
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }
    require(failure.starts_with("AADF queue requires "),
            "Unsupported AADF queue did not fail during capacity preflight");
    require(failure.find(std::to_string(target.bytes)) != std::string::npos &&
                failure.find(std::to_string(
                    compute.device.properties.limits.maxStorageBufferRange)) != std::string::npos,
            "AADF failure did not report padded required bytes and the actual device limit");
}

void queue_capacity_contract(renderer::WorldCompute& compute) {
    const auto source = source_model(2);
    const auto previous = renderer::generate_world(compute, source, {64, 64, 64}, 1);
    const auto buffers = compute.buffers;
    const auto chunks = compute.chunks;
    const auto executions = compute.execution_counts();
    const auto target = unsupported_queue(compute.device);
    reject_queue_capacity(compute, source, target, false);
    reject_queue_capacity(compute, source, target, true);
    require(compute.buffers == buffers && compute.chunks.geometry == chunks.geometry &&
                compute.chunks.entities == chunks.entities,
            "Capacity refusal replaced resources of the existing world");
    require(compute.execution_counts() == executions,
            "Capacity refusal performed GPU generation or queue initialization");
    compare_bounds(compute, previous);
    std::cout << "AADF queue capacity " << target.bytes
              << " rejected before allocation; real limit "
              << compute.device.properties.limits.maxStorageBufferRange
              << "; prior GPU world remained readable\n";
}

void run_case(renderer::WorldCompute& compute, int pattern, model::IVec3 size) {
    const auto source = source_model(pattern);
    const auto result = renderer::generate_world(compute, source, size, 1);
    compare_voxels(source, result);
    compare_hash(result);
    compare_bounds(compute, result);
    if (pattern == 2 && size == model::IVec3{64, 16, 64}) {
        require(result.logical_voxels == 128 && result.block_words == 1088,
                "Repeated mixed block was not deduplicated to one allocation");
        require(result.compression == 1024, "Repeated-block compression differs from 1024");
    }
    if (pattern == 3) {
        require(result.logical_voxels == (4096 + 1) * 64,
                "Distinct blocks did not allocate exactly 4096 ranges");
        require(result.compression == 2 && result.segments == 2,
                "Two-segment source repetition changed dedup references");
    }
}

void run_contracts(renderer::WorldCompute& compute) {
    // A reduced cached dispatch limit exercises Vulkan partitioning on small inputs.
    compute.device.properties.limits.maxComputeWorkGroupCount[0] = 64;
    run_case(compute, 0, {31, 19, 17});
    run_case(compute, 1, {64, 16, 64});
    run_case(compute, 2, {64, 16, 64});
    run_case(compute, 2, {29, 11, 37});
    run_case(compute, 3, {128, 64, 64});
    for (const auto* entry :
         {"world_generator_generatorModel_fillChunkDataWithModelData16",
          "world_data_chunkCalc_calcBlockFromRawData", "world_data_mapCopy_copyMap",
          "world_data_chunkCalc_chunkCopyToCpu", "world_data_chunkCalc_computeVoxelBounds",
          "world_data_chunkCalc_computeBlockBounds"}) {
        require(compute.execution_counts().contains(entry),
                "A production generation entry was unused");
    }
}

void check_segment_settings(renderer::WorldCompute& compute) {
    const auto source = source_model(0);
    const auto maximum = compute.device.properties.limits.maxStorageBufferRange;
    // Force physical splitting while retaining each original logical padded extent.
    compute.device.properties.limits.maxStorageBufferRange = 1024 * 1024;
    for (const auto& [groups, segments] :
         {std::pair{3, 27U}, std::pair{5, 64U}, std::pair{6, 216U}, std::pair{7, 343U}}) {
        const auto result = renderer::generate_world(compute, source, {1, 1, 1}, groups);
        const auto side = groups * 64;
        require(result.model.size == model::IVec3{side, side, side},
                "Physical segment limit changed logical world padding");
        require(result.segments == segments,
                "Generation did not split segments to the physical storage limit");
        require(std::ranges::all_of(result.model.chunks, [](auto word) { return word == 0; }),
                "Split empty generation wrote nonempty chunks");
    }
    compute.device.properties.limits.maxStorageBufferRange = maximum;
    const auto non_power = renderer::generate_world(compute, source, {1, 1, 1}, 3);
    require(std::has_single_bit(non_power.hash.size()),
            "Non-power-of-two group size produced invalid hash mask");
    require(non_power.segments == 1, "Supported physical segment was unnecessarily split");
    require(compute.buffers.size() == 2, "Generation retained transient GPU buffers");
    std::cout << "Group sizes 3/5/6/7 and forced 1 MiB storage limit retained original geometry\n";
}

void sampled_source(const model::Model& source, const renderer::GenerationResult& result) {
    std::uint32_t state = 0x12345678U;
    for (unsigned sample = 0; sample < 100000; ++sample) {
        model::IVec3 point;
        for (auto* coordinate : {&point.x, &point.y, &point.z}) {
            state = state * 1664525U + 1013904223U;
            *coordinate = static_cast<int>(state >> 8);
        }
        point.x %= result.model.size.x;
        point.y %= result.model.size.y;
        point.z %= result.model.size.z;
        require(model::voxel_at(result.model, point) ==
                    expected_at(source, result.actual_size, point),
                "Large generated world sample differs from original source");
    }
}

model::Model capacity_source() {
    model::Model source;
    source.size = {256, 256, 320};
    source.materials.resize(32768);
    source.chunks.resize(16 * 16 * 20);
    source.blocks.resize(source.chunks.size() * 64 + 64);
    source.voxels.resize(source.chunks.size() * 64 * 32 + 32);
    for (std::size_t index = 0; index < source.chunks.size(); ++index) {
        source.chunks[index] = 0x80000000U | static_cast<std::uint32_t>((index + 1) * 64);
    }
    for (std::size_t index = 0; index < source.chunks.size() * 64; ++index) {
        const auto offset = static_cast<std::uint32_t>((index + 1) * 32);
        source.blocks[index + 64] = 0x80000000U | offset;
        const auto low = static_cast<std::uint32_t>(index % 32767 + 1);
        const auto high = static_cast<std::uint32_t>(index / 32767 + 1);
        source.voxels[offset] = 0x80008000U | low | (high << 16);
    }
    return source;
}

void source_capacity_contract(renderer::WorldCompute& compute) {
    constexpr std::uint32_t range = 128U * 1024U * 1024U;
    const auto maximum = compute.device.properties.limits.maxStorageBufferRange;
    if (maximum < range) {
        return;
    }
    compute.device.properties.limits.maxStorageBufferRange = range;
    const auto source = capacity_source();
    const auto result = renderer::generate_world(compute, source, source.size, 6);
    require(result.model.size == model::IVec3{384, 384, 384} && result.segments == 8,
            "Source plus next-segment reserve exceeded the storage range");
    require(result.logical_voxels == source.voxels.size() * 2 &&
                result.block_words == source.blocks.size() && result.compression == 1,
            "Distinct source blocks changed through storage-limited generation");
    sampled_source(source, result);
    compute.device.properties.limits.maxStorageBufferRange = maximum;
    std::cout << "40 MiB source capacity + group6 fits 128 MiB through eight physical segments\n";
}

void collision_contract(renderer::WorldCompute& compute) {
    std::vector<model::Sample> samples;
    constexpr int count = 512;
    for (int index = 0; index < count; ++index) {
        const model::IVec3 base{(index % 8) * 4, ((index / 8) % 8) * 4, (index / 64) * 4};
        samples.push_back({base, static_cast<std::uint16_t>(index + 1)});
        samples.push_back(
            {{base.x + 1, base.y, base.z}, static_cast<std::uint16_t>(31 * (count - index))});
    }
    const auto source =
        model::build_model({32, 32, 32}, std::vector<model::Material>(31 * count + 1), samples);
    // Every distinct block has exactly the same original 31-polynomial hash.
    const auto result = renderer::generate_world(compute, source, {32, 32, 32}, 1);
    compare_voxels(source, result);
    compare_hash(result);
    compare_bounds(compute, result);
    require(result.logical_voxels == (count + 1) * 64,
            "Collision chain lost distinct mixed blocks");
    require(result.compression == 1, "Hash collision was treated as a duplicate block");
    std::cout << "512 distinct same-hash blocks survived allocation and GPU rehash\n";
}

void run_oasis(renderer::WorldCompute& compute, const char* path) {
    const auto source = engine::world::storage::load_cvox(path);
    std::cout << "Source " << source.size.x << 'x' << source.size.y << 'x' << source.size.z
              << " words " << source.chunks.size() << '/' << source.blocks.size() << '/'
              << source.voxels.size() << '\n';
    const auto start = std::chrono::steady_clock::now();
    const auto result = renderer::generate_world(compute, source, {4096, 512, 4096}, 4);
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    sampled_source(source, result);
    std::uint64_t allocation = compute.chunks.allocation();
    for (const auto& [name, buffer] : compute.buffers) {
        allocation += buffer->allocation;
        std::cout << name << " allocation " << buffer->allocation << '\n';
    }
    std::cout << "Full Oasis GPU seconds " << seconds << " segments " << result.segments
              << " chunks " << result.model.chunks.size() << " blocks " << result.block_words
              << " logical_voxels " << result.logical_voxels << " compression "
              << result.compression << " steady_gpu_bytes " << allocation
              << " sampled_voxels 100000 matched\n";
    for (const auto& [name, count] : compute.execution_counts()) {
        std::cout << name << " dispatches " << count << '\n';
    }
}

std::vector<std::uint32_t> image_words(renderer::WorldCompute& compute,
                                       const renderer::Image& image, std::size_t count) {
    auto staging = renderer::make_buffer(compute.device,
                                         {count * 4, vk::BufferUsageFlagBits::eTransferDst, true});
    auto command = compute.command();
    renderer::image_barrier(command, *image.handle, vk::ImageLayout::eGeneral,
                            vk::ImageLayout::eTransferSrcOptimal);
    vk::BufferImageCopy copy;
    copy.setImageSubresource(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
        .setImageExtent(image.extent);
    command.copyImageToBuffer(*image.handle, vk::ImageLayout::eTransferSrcOptimal, *staging->handle,
                              copy);
    renderer::image_barrier(command, *image.handle, vk::ImageLayout::eTransferSrcOptimal,
                            vk::ImageLayout::eGeneral);
    const vk::MemoryBarrier2 host(
        vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead);
    command.pipelineBarrier2(vk::DependencyInfo({}, host));
    compute.synchronize();
    const auto* words = static_cast<const std::uint32_t*>(staging->mapped);
    return {words, words + count};
}

void uniform_contract(renderer::WorldCompute& compute, model::IVec3 size) {
    model::Model source;
    source.size = size;
    source.materials.resize(4);
    const auto chunks = model::chunk_size(size);
    source.chunks.resize(std::size_t(chunks.x) * chunks.y * chunks.z);
    source.blocks.resize(64);
    source.voxels.resize(32);
    for (std::size_t index = 0; index < source.chunks.size(); ++index) {
        const auto cell = static_cast<std::uint32_t>((index % chunks.x) / 2 +
                                                     3 * ((index / chunks.x) % chunks.y) / 2 +
                                                     5 * (index / chunks.x / chunks.y) / 2);
        const auto material = cell % 4;
        source.chunks[index] = material == 0 ? 0 : 0x40000000U | material;
    }
    const auto result = renderer::generate_world(compute, source, size, 1);
    require(result.segments == 0 && result.model.chunks == source.chunks,
            "Prepacked scene did not preserve its exact chunk graph");
    if (compute.chunks.packed()) {
        const auto words = image_words(compute, *compute.chunks.geometry, source.chunks.size() * 2);
        for (std::size_t index = 0; index < source.chunks.size(); ++index) {
            require(words[index * 2] == source.chunks[index],
                    "Packed GPU uniform geometry differs across batch/axis boundaries");
            require(words[index * 2 + 1] == 0,
                    "Packed imported scene did not clear entity membership");
        }
    } else {
        require(image_words(compute, *compute.chunks.geometry, source.chunks.size()) ==
                    source.chunks,
                "GPU uniform-chunk image differs from CPU source across batch/axis boundaries");
        const auto entities = image_words(compute, *compute.chunks.entities, source.chunks.size());
        require(std::ranges::all_of(entities, [](auto word) { return word == 0; }),
                "Imported scene did not clear entity membership");
    }
    compare_hash(result);
    compare_bounds(compute, result);
    sampled_source(source, result);
    std::cout << "Uniform CPU/GPU chunks, entity clear, reserved AADF and sampled voxels match: "
              << source.chunks.size() << " chunks\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto layout = renderer::ChunkLayout::automatic;
        if (argc > 2 && (std::string_view(argv[argc - 1]) == "--packed" ||
                         std::string_view(argv[argc - 1]) == "--split")) {
            layout = renderer::test::parse_chunk_layout(argv[--argc]);
        }
        require(argc == 2 || argc == 3,
                "Usage: rendering_generation_test SHADER_DIRECTORY [OASIS_CVOX] "
                "[--packed|--split]");
        auto window = create_window();
        renderer::Device device(window, true, layout);
        renderer::BufferMap buffers;
        renderer::ChunkImages chunks;
        renderer::WorldCompute compute(device, argv[1], buffers, chunks);
        if (argc == 3 && std::string_view(argv[2]) == "--uniform") {
            uniform_contract(compute, {64, 128, 64});
            uniform_contract(compute, {4096, 1024, 4096});
        } else if (argc == 3 && std::string_view(argv[2]) == "--collisions") {
            collision_contract(compute);
        } else if (argc == 3) {
            run_oasis(compute, argv[2]);
        } else {
            queue_capacity_contract(compute);
            const auto dispatch_limit = device.properties.limits.maxComputeWorkGroupCount[0];
            run_contracts(compute);
            check_segment_settings(compute);
            device.properties.limits.maxComputeWorkGroupCount[0] = dispatch_limit;
            source_capacity_contract(compute);
            collision_contract(compute);
        }
        require(device.validation_errors.load() == 0, "Generation Vulkan validation failed");
        std::cout << "Original GPU generation, dedup, rehash, readback, and local AADF passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
