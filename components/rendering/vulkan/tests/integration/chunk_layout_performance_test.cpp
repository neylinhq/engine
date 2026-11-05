#include "../support/chunks/performance.hpp"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
namespace renderer = engine::rendering::vulkan;
using Options = renderer::test::ChunkPerformanceOptions;

std::uint32_t number(std::string_view text, std::uint32_t maximum) {
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value > maximum) {
        throw std::runtime_error("Invalid unsigned argument: " + std::string(text));
    }
    return value;
}

std::string_view argument(int argc, char** argv, int& index) {
    if (++index >= argc) {
        throw std::runtime_error("Missing value after " + std::string(argv[index - 1]));
    }
    return argv[index];
}

renderer::ChunkLayout layout(std::string_view text) {
    if (text == "packed") {
        return renderer::ChunkLayout::packed;
    }
    if (text == "split") {
        return renderer::ChunkLayout::split;
    }
    throw std::runtime_error("Layout must be packed or split");
}

renderer::test::ChunkPattern pattern(std::string_view text) {
    if (text == "uniform") {
        return renderer::test::ChunkPattern::uniform;
    }
    if (text == "varied") {
        return renderer::test::ChunkPattern::varied;
    }
    throw std::runtime_error("Pattern must be uniform or varied");
}

void scalar_option(Options& options, std::string_view name, std::string_view value) {
    if (name == "--shaders") {
        options.shaders = value;
    } else if (name == "--rays") {
        options.rays = number(value, std::numeric_limits<std::uint32_t>::max());
    } else if (name == "--frames") {
        options.frames = number(value, 1000000);
    } else if (name == "--warmup") {
        options.warmup = number(value, 1000000);
    } else if (name == "--layout") {
        options.layout = layout(value);
    } else if (name == "--pattern") {
        options.pattern = pattern(value);
    } else {
        throw std::runtime_error("Unknown option: " + std::string(name));
    }
}

Options parse(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view name = argv[index];
        if (name == "--help") {
            options.help = true;
        } else if (name == "--validation") {
            options.validation = true;
        } else if (name == "--size") {
            options.size[0] = number(argument(argc, argv, index), 32768);
            options.size[1] = number(argument(argc, argv, index), 16384);
            options.size[2] = number(argument(argc, argv, index), 32768);
        } else {
            scalar_option(options, name, argument(argc, argv, index));
        }
    }
    if (!options.help && (options.frames == 0 || options.rays == 0)) {
        throw std::runtime_error("At least one measured frame and one ray are required");
    }
    return options;
}

void help() {
    std::cout
        << "Synthetic production shootRay chunk-layout diagnostic; requires a display.\n"
           "This does not build a world, maintain AADF, render frames or measure application FPS.\n"
           "  --shaders DIR      Directory with trace_probe.spv/json and packed/ variant\n"
           "  --size X Y Z       Voxel extent, whole 16-voxel chunks; default 4096 512 4096\n"
           "  --layout MODE      packed|split; default split\n"
           "  --pattern MODE     uniform|varied empty bounds; default uniform\n"
           "  --rays N           Fixed deterministic ray count; default 65536\n"
           "  --frames N         Measured dispatches after warmup; default 16\n"
           "  --warmup N         Additional untimed dispatches; default 4\n"
           "  --validation       Enable Khronos validation (off for timing by default)\n"
           "CSV goes to stdout; metadata to stderr. Image initialization, uploads and readback are "
           "untimed.\n";
}

void pattern_metadata(const Options& options, const renderer::test::ChunkPatternInfo& pattern) {
    const auto varied = options.pattern == renderer::test::ChunkPattern::varied;
    std::cerr << "pattern=" << (varied ? "varied" : "uniform")
              << "\ngeometry_initialization=" << (varied ? "tiled_buffer_upload" : "image_clear")
              << "\ngeometry_word_algorithm="
              << (varied ? "pcg_index_nonzero_5bit_v1" : "constant_0x3fffffff")
              << "\ngeometry_field_range=" << (varied ? "1..31" : "31")
              << "\ngeometry_word_hash64=" << pattern.geometry_fingerprint
              << "\ngeometry_hash_algorithm=uint32_polynomial64_v1"
                 "\ngeometry_hash_initial=14695981039346656037"
                 "\ngeometry_hash_multiplier=1099511628211"
                 "\ngeometry_hash_order=x_fastest_then_y_then_z"
                 "\nentity_initial_value=0\n"
              << "homogeneous_geometry_clear=" << !varied
              << "\ndriver_compression_not_disabled=true"
                 "\nstaging_payload_bytes="
              << pattern.staging_payload_bytes
              << "\nstaging_allocation_bytes=" << pattern.staging_allocation_bytes
              << "\nstaging_released_before_warmup=true"
                 "\nverified_chunk_pattern_samples="
              << pattern.verified_samples << '\n';
    if (!varied) {
        std::cerr << "geometry_clear=0x3fffffff\nentity_clear=0\n"
                     "homogeneous_clear_may_use_driver_compression=true\n";
    }
}

void metadata(const Options& options, const renderer::test::ChunkLayoutPerformance& test,
              const renderer::test::ChunkPerformanceResult& result) {
    const auto& device = test.device();
    std::cerr << "schema=engine.chunk_layout_performance.v1\n"
              << "scope=synthetic_empty_volume_production_shootRay_only\n"
              << "device=" << std::quoted(device.properties.deviceName.data()) << '\n'
              << "layout=" << (device.packed_chunks ? "packed" : "split") << '\n'
              << "voxel_extent=" << options.size[0] << ',' << options.size[1] << ','
              << options.size[2] << "\nchunk_extent=" << options.size[0] / 16 << ','
              << options.size[1] / 16 << ',' << options.size[2] / 16
              << "\nchunk_image_allocation_bytes=" << result.image_bytes
              << "\nowned_gpu_allocation_bytes=" << result.allocated_bytes
              << "\naadf_queue_bytes=0\ncpu_chunk_mirror_bytes=0\n";
    pattern_metadata(options, result.pattern);
    std::cerr << "ray_seed=" << renderer::test::kChunkRaySeed
              << "\nray_input_fnv1a64=" << result.ray_fingerprint << "\nray_count=" << options.rays
              << "\nray_step_cap=2000\nidentical_rays_reused_each_dispatch=true\n"
              << "warmup_dispatches=" << options.warmup
              << "\nmeasured_dispatches=" << options.frames
              << "\nverified_final_miss_records=" << result.verified_misses
              << "\nbinding_allocations=" << result.binding_allocations
              << "\nvalidation_enabled=" << options.validation
              << "\nvalidation_errors=" << device.validation_errors.load()
              << "\ntiming_scope=dispatch_and_following_dependency_barrier\n"
              << "allocation_scope=owned_images_buffers_uniforms_excludes_driver_query_pools\n";
}

void csv(const Options& options, const renderer::test::ChunkPerformanceResult& result) {
    std::cout << "frame,gpu_trace_ms,rays,megarays_per_second\n"
              << std::fixed << std::setprecision(9);
    for (std::size_t frame = 0; frame < result.gpu_milliseconds.size(); ++frame) {
        const auto milliseconds = result.gpu_milliseconds[frame];
        std::cout << frame << ',' << milliseconds << ',' << options.rays << ',';
        if (milliseconds > 0) {
            std::cout << static_cast<double>(options.rays) / (milliseconds * 1000);
        }
        std::cout << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        if (options.help) {
            help();
            return 0;
        }
        renderer::test::ChunkLayoutPerformance test(options);
        const auto result = test.run();
        metadata(options, test, result);
        csv(options, result);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
