#include <engine/rendering/vulkan/renderer.hpp>
#include <engine/world/editing/editing.hpp>
#include <engine/world/storage/storage.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
namespace rendering = engine::rendering;
namespace model = engine::world::model;
namespace camera = engine::motion::camera;
namespace window = engine::platform::window;
using Clock = std::chrono::steady_clock;

struct Options {
    std::filesystem::path shaders = "shaders";
    std::filesystem::path source;
    std::filesystem::path pass_output;
    std::filesystem::path screenshot;
    std::filesystem::path edit_output;
    model::Vec3i size{256, 256, 256};
    unsigned frames = 32;
    unsigned warmup = 8;
    unsigned budget = 32768;
    unsigned width = 320;
    unsigned height = 180;
    unsigned segment_groups = 4;
    unsigned bounces = 3;
    bool skip_samples = true;
    rendering::settings::RenderVersion mode = rendering::settings::RenderVersion::base;
    rendering::vulkan::ChunkLayout chunk_layout = rendering::vulkan::ChunkLayout::automatic;
    rendering::vulkan::PresentationMode presentation =
        rendering::vulkan::PresentationMode::automatic;
    engine::foundation::geometry::Vec3 eye{12, 26, 12};
    engine::foundation::geometry::Vec3 brush_center{12, 12, 12};
    double yaw = 0;
    double pitch = -0.4;
    double fov = 90;
    bool empty = false;
    bool validation = false;
    bool visible = false;
    bool borderless = false;
    bool motion = false;
    bool help = false;
    bool defer_frame_wait = true;
    bool offscreen_output = false;
};

struct Sample {
    std::uint64_t submitted_frame = 0;
    std::uint64_t completed_frame = 0;
    double cpu_frame_ms = 0;
    double cpu_begin_ms = 0;
    double cpu_update_submit_ms = 0;
    double cpu_wait_ms = 0;
    double cpu_record_ms = 0;
    double cpu_submit_ms = 0;
    double gpu_total_ms = 0;
    double gpu_prepare_ms = 0;
    double gpu_render_ms = 0;
    std::uint64_t allocated_bytes = 0;
    std::uint64_t entity_upload_bytes = 0;
    std::uint64_t world_dispatches = 0;
    std::uint32_t render_passes = 0;
    std::vector<rendering::vulkan::PassTiming> gpu_passes;
};

template <typename Value> Value take(std::expected<Value, std::string> result) {
    if (!result) {
        throw std::runtime_error(result.error());
    }
    return std::move(*result);
}

void require(std::expected<void, std::string> result) {
    if (!result) {
        throw std::runtime_error(result.error());
    }
}

unsigned number(std::string_view text, unsigned maximum) {
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > maximum) {
        throw std::runtime_error("Invalid integer argument: " + std::string(text));
    }
    return value;
}

double real_number(std::string_view text) {
    double value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(value)) {
        throw std::runtime_error("Invalid real argument: " + std::string(text));
    }
    return value;
}

rendering::vulkan::ChunkLayout chunk_layout(std::string_view value) {
    if (value == "automatic") {
        return rendering::vulkan::ChunkLayout::automatic;
    }
    if (value == "packed") {
        return rendering::vulkan::ChunkLayout::packed;
    }
    if (value == "split") {
        return rendering::vulkan::ChunkLayout::split;
    }
    throw std::runtime_error("Unknown chunk layout: " + std::string(value));
}

bool deferred_wait(std::string_view value) {
    if (value == "deferred") {
        return true;
    }
    if (value == "early") {
        return false;
    }
    throw std::runtime_error("Unknown frame wait placement: " + std::string(value));
}

bool offscreen_output(std::string_view value) {
    if (value == "automatic") {
        return false;
    }
    if (value == "offscreen") {
        return true;
    }
    throw std::runtime_error("Unknown output path: " + std::string(value));
}

rendering::vulkan::PresentationMode presentation_mode(std::string_view value) {
    using rendering::vulkan::PresentationMode;
    if (value == "automatic") {
        return PresentationMode::automatic;
    }
    if (value == "immediate") {
        return PresentationMode::immediate;
    }
    if (value == "mailbox") {
        return PresentationMode::mailbox;
    }
    if (value == "fifo") {
        return PresentationMode::fifo;
    }
    throw std::runtime_error("Unknown presentation mode: " + std::string(value));
}

std::string_view argument(int argc, char** argv, int& index) {
    if (++index >= argc) {
        throw std::runtime_error("Missing argument after " + std::string(argv[index - 1]));
    }
    return argv[index];
}

rendering::settings::RenderVersion render_mode(std::string_view value) {
    if (value == "albedo") {
        return rendering::settings::RenderVersion::albedo;
    }
    if (value == "base") {
        return rendering::settings::RenderVersion::base;
    }
    if (value == "path_tracer") {
        return rendering::settings::RenderVersion::path_tracer;
    }
    throw std::runtime_error("Unknown renderer: " + std::string(value));
}

bool flag(Options& options, std::string_view name) {
    if (name == "--empty") {
        options.empty = true;
    } else if (name == "--validation") {
        options.validation = true;
    } else if (name == "--visible") {
        options.visible = true;
    } else if (name == "--borderless") {
        options.borderless = true;
        options.visible = true;
    } else if (name == "--motion") {
        options.motion = true;
    } else if (name == "--help") {
        options.help = true;
    } else {
        return false;
    }
    return true;
}

void scalar_option(Options& options, std::string_view name, std::string_view value) {
    if (name == "--bounces") {
        options.bounces = number(value, 10);
    } else if (name == "--skip-samples") {
        options.skip_samples = number(value, 1) != 0;
    } else if (name == "--frames") {
        options.frames = number(value, 1000000);
    } else if (name == "--warmup") {
        options.warmup = number(value, 1000000);
    } else if (name == "--budget") {
        options.budget = number(value, 262144);
    } else if (name == "--width") {
        options.width = number(value, 16384);
    } else if (name == "--height") {
        options.height = number(value, 16384);
    } else if (name == "--segment-groups") {
        options.segment_groups = number(value, 8);
    } else {
        throw std::runtime_error("Unknown option: " + std::string(name));
    }
}

void value_option(Options& options, std::string_view name, std::string_view value) {
    if (name == "--shaders") {
        options.shaders = value;
    } else if (name == "--source") {
        options.source = value;
    } else if (name == "--mode") {
        options.mode = render_mode(value);
    } else if (name == "--chunk-layout") {
        options.chunk_layout = chunk_layout(value);
    } else if (name == "--frame-wait") {
        options.defer_frame_wait = deferred_wait(value);
    } else if (name == "--output-path") {
        options.offscreen_output = offscreen_output(value);
    } else if (name == "--passes") {
        options.pass_output = value;
    } else if (name == "--screenshot") {
        options.screenshot = value;
    } else if (name == "--edit-output") {
        options.edit_output = value;
    } else if (name == "--present-mode") {
        options.presentation = presentation_mode(value);
    } else if (name == "--yaw") {
        options.yaw = real_number(value);
    } else if (name == "--pitch") {
        options.pitch = real_number(value);
    } else if (name == "--fov") {
        options.fov = real_number(value);
    } else {
        scalar_option(options, name, value);
    }
}

void validate_options(const Options& options) {
    if (options.size.x < 64 || options.size.y < 64 || options.size.z < 64 || options.width == 0 ||
        options.height == 0 || options.segment_groups == 0 || options.frames == 0 ||
        options.frames + options.warmup > 1000000) {
        throw std::runtime_error("World sides must be at least 64; dimensions, frame count and "
                                 "segment groups must be positive; total frames <= 1000000");
    }
    if (options.empty && !options.source.empty()) {
        throw std::runtime_error("Choose either --source or --empty");
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view name = argv[index];
        if (flag(options, name)) {
            continue;
        }
        if (name == "--size") {
            options.size.x = static_cast<int>(number(argument(argc, argv, index), 16384));
            options.size.y = static_cast<int>(number(argument(argc, argv, index), 16384));
            options.size.z = static_cast<int>(number(argument(argc, argv, index), 16384));
        } else if (name == "--eye" || name == "--brush-center") {
            auto& position = name == "--eye" ? options.eye : options.brush_center;
            position.x = real_number(argument(argc, argv, index));
            position.y = real_number(argument(argc, argv, index));
            position.z = real_number(argument(argc, argv, index));
        } else {
            value_option(options, name, argument(argc, argv, index));
        }
    }
    validate_options(options);
    return options;
}

void usage() {
    std::cout << "Usage: rendering_large_world_performance_test [options]\n"
                 "  --shaders DIRECTORY   Compiled production Slang programs\n"
                 "  --size X Y Z          Requested world extent (default 256 256 256)\n"
                 "  --frames N            Measured frames (default 32)\n"
                 "  --warmup N            Tagged warmup frames (default 8)\n"
                 "  --budget N            AADF groups/iteration, 0..262144 (default 32768)\n"
                 "  --source FILE         Canonical model; default is the fixed floor fixture\n"
                 "  --empty               Empty fixture, mutually exclusive with --source\n"
                 "  --segment-groups N    Logical generation segment, 1..8 (default 4)\n"
                 "  --mode MODE           albedo, base, path_tracer (default base)\n"
                 "  --bounces N           Secondary bounces, 0..10 (default 3)\n"
                 "  --skip-samples 0|1    Quarter-rate Base sampling (default 1)\n"
                 "  --width N --height N  Fixed framebuffer (default 320 x 180)\n"
                 "  --eye X Y Z          Camera eye in voxels (default 12 26 12)\n"
                 "  --yaw N --pitch N    Camera angles in radians (default 0, -0.4)\n"
                 "  --fov N              Vertical field of view in degrees (default 90)\n"
                 "  --chunk-layout MODE  automatic, packed or split (default automatic)\n"
                 "  --frame-wait MODE    deferred or early completion wait (default deferred)\n"
                 "  --output-path MODE   automatic or offscreen (default automatic)\n"
                 "  --passes FILE        Opt-in per-pass GPU timestamps, written as CSV\n"
                 "  --present-mode MODE  automatic, immediate, mailbox or fifo\n"
                 "  --screenshot FILE    Save a frame after measurements\n"
                 "  --edit-output FILE   Measure sphere fill/erase after static frames\n"
                 "  --brush-center X Y Z Edit benchmark center in voxel coordinates\n"
                 "  --validation          Enable Vulkan validation (reported in metadata)\n"
                 "  --visible             Show the window; no user interface is drawn\n"
                 "  --borderless          Use the current monitor's full desktop extent\n"
                 "  --motion              Translate, rotate and cut the camera after warmup\n"
                 "CSV samples go to stdout; metadata and errors go to stderr. GPU timings\n"
                 "match completed submission IDs; warmup is retained and explicitly tagged.\n";
}

model::Model fixture(bool empty) {
    std::vector<model::Sample> samples;
    if (!empty) {
        samples.reserve(32 * 32);
        for (int z = 0; z < 32; ++z) {
            for (int x = 0; x < 32; ++x) {
                samples.push_back({{x, 0, z}, 1});
            }
        }
    }
    std::vector<model::Material> materials(2);
    materials[1].name = "performance_floor";
    materials[1].color_base = {0.2, 0.6, 0.1};
    return model::build_model({32, 16, 32}, std::move(materials), samples);
}

double milliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

bool software_name(std::string device) {
    for (auto& letter : device) {
        letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    }
    return device.find("llvmpipe") != std::string::npos ||
           device.find("lavapipe") != std::string::npos ||
           device.find("swiftshader") != std::string::npos ||
           device.find("software") != std::string::npos;
}

std::string_view mode_name(rendering::settings::RenderVersion mode) {
    if (mode == rendering::settings::RenderVersion::albedo) {
        return "albedo";
    }
    return mode == rendering::settings::RenderVersion::base ? "base" : "path_tracer";
}

void extent_metadata(std::string_view name, model::Vec3i size) {
    std::cerr << name << '=' << size.x << ',' << size.y << ',' << size.z << '\n';
}

void generation_metadata(const Options& options, const model::Model& source) {
    const auto segment = static_cast<int>(options.segment_groups) * 64;
    const model::Vec3i padded{(options.size.x + segment - 1) / segment * segment,
                              (options.size.y + segment - 1) / segment * segment,
                              (options.size.z + segment - 1) / segment * segment};
    const auto groups = std::uint64_t(padded.x / 64) * (padded.y / 64) * (padded.z / 64);
    extent_metadata("requested_size", options.size);
    extent_metadata("expected_padded_size", padded);
    extent_metadata("source_size", source.size);
    std::cerr << "expected_aadf_group_count=" << groups << '\n'
              << "expected_aadf_queue_bytes=" << groups * 96 * 4 << '\n'
              << "source="
              << (options.source.empty() ? (options.empty ? "empty" : "floor_fixture")
                                         : options.source.string())
              << '\n';
}

rendering::vulkan::Configuration configuration(const Options& options) {
    rendering::vulkan::Configuration result;
    result.shaders = options.shaders;
    result.validation = options.validation;
    result.profile_world_updates = true;
    result.profile_render_passes = !options.pass_output.empty();
    result.chunk_layout = options.chunk_layout;
    result.defer_frame_wait = options.defer_frame_wait;
    result.offscreen_output = options.offscreen_output;
    result.presentation_mode = options.presentation;
    return result;
}

struct Harness {
    model::World world{std::thread::hardware_concurrency()};
    camera::Camera view;
    window::Window window;
    rendering::vulkan::Renderer renderer;
    rendering::settings::Settings settings;

    explicit Harness(const Options& options)
        : view(take(camera::Camera::create(16384, {options.eye, options.yaw, options.pitch}))),
          window(take(window::Window::create(
              {static_cast<int>(options.width), static_cast<int>(options.height),
               "NAADF performance investigation", options.visible}))),
          renderer(take(rendering::vulkan::Renderer::create(window, configuration(options)))) {
        if (options.borderless) {
            require(window.toggle_borderless());
        }
        settings.render.version = options.mode;
        settings.render.base.bounce_count = static_cast<int>(options.bounces);
        settings.render.path_tracer.bounce_count = static_cast<int>(options.bounces);
        settings.render.base.skip_samples = options.skip_samples;
        settings.general.fov = static_cast<float>(options.fov);
        auto configuration = view.settings();
        configuration.fov_degrees = settings.general.fov;
        require(view.configure(configuration));
        view.clear_changed();
    }

    void generate(const Options& options) {
        auto source = options.source.empty() ? fixture(options.empty)
                                             : engine::world::storage::load(options.source);
        generation_metadata(options, source);
        const auto begin = Clock::now();
        auto mapped = world.prepare_generation(std::move(source));
        require(renderer.generate_world(world, mapped, options.size,
                                        static_cast<int>(options.segment_groups)));
        std::cerr << "generation_cpu_ms=" << milliseconds(begin, Clock::now()) << '\n';
        if (!world.view().gpu_authoritative || world.actual_size() != options.size) {
            throw std::runtime_error("Production GPU world generation changed requested extent");
        }
        extent_metadata("actual_padded_size", world.size());
        const auto data = world.view();
        std::cerr << "cpu_chunk_bytes=" << data.chunks.size_bytes() << '\n'
                  << "cpu_entity_chunk_bytes=" << world.entity_gpu().chunks.size() * 4 << '\n'
                  << "cpu_block_bytes=" << data.blocks.size_bytes() << '\n'
                  << "cpu_voxel_bytes=" << data.voxels.size_bytes() << '\n'
                  << "initial_gpu_allocated_bytes=" << renderer.statistics().allocated_bytes
                  << '\n';
    }

    void draw(unsigned budget, bool active, bool capture_output = false) {
        if (active) {
            world.update_entities(1.0 / 60.0, 0, renderer.history_index(settings.render.version));
        }
        rendering::vulkan::FrameInput input;
        input.world = world.view();
        input.entities = &world.entity_gpu();
        input.camera = &view;
        input.settings = &settings;
        input.delta_seconds = 1.0 / 60.0;
        input.bounds_budget = budget;
        input.active = active;
        input.paused = !active;
        input.capture_output = capture_output;
        require(renderer.draw(input));
        if (active) {
            world.acknowledge_changes();
        }
    }
};

void timing_metadata() {
    std::cerr << "simulation_delta_seconds=0.016666666666666667\n"
                 "cpu_frame_scope=poll,begin_frame,world_update,draw_submit_present\n"
                 "gpu_completion_capture=after_draw_by_completed_submission_id\n"
                 "cpu_begin_scope=poll,acquire,imgui_new_frame;"
                 "previous_submission_wait_only_in_early_mode\n"
                 "cpu_update_submit_scope=world_update,draw_submit_present;"
                 "previous_submission_wait_only_in_deferred_mode\n"
                 "cpu_gpu_alignment=cpu_row_N_waits_for_GPU_N_minus_1;"
                 "cpu_frame_ms_is_not_GPU_N_latency\n"
                 "warmup_policy=explicit_frame_count_tag_not_a_steady_state_guarantee\n"
                 "gpu_prepare_scope=screen_initialization,world_upload,entities,edits,aadf\n"
                 "gpu_render_scope=render_passes,output,optional_presentation_copy\n"
                 "memory_scope=renderer_allocations_not_driver_residency\n";
}

void run_metadata(const Options& options, const rendering::vulkan::Statistics& statistics) {
    std::cerr << "device=" << statistics.device << '\n'
              << "device_class="
              << (software_name(statistics.device) ? "software_name_match" : "unclassified") << '\n'
              << "build_type=" << ENGINE_RENDERING_VULKAN_PERFORMANCE_BUILD_TYPE << '\n'
              << "validation=" << options.validation << '\n'
              << "gpu_timestamps_supported=" << statistics.gpu_timestamps_supported << '\n'
              << "world_updates_profiled=" << statistics.world_updates_profiled << '\n'
              << "render_passes_profiled=" << statistics.render_passes_profiled << '\n'
              << "frame_wait=" << (options.defer_frame_wait ? "deferred" : "early") << '\n'
              << "output_path_requested=" << (options.offscreen_output ? "offscreen" : "automatic")
              << '\n'
              << "output_path_actual=" << (statistics.direct_output ? "direct" : "offscreen")
              << '\n'
              << "chunk_layout="
              << (statistics.chunk_layout == rendering::vulkan::ChunkLayout::packed ? "packed"
                                                                                    : "split")
              << '\n'
              << "render_mode=" << mode_name(options.mode) << '\n'
              << "bounces=" << options.bounces << '\n'
              << "skip_samples=" << options.skip_samples << '\n'
              << "requested_render_extent=" << options.width << ',' << options.height << '\n'
              << "actual_render_extent=" << statistics.width << ',' << statistics.height << '\n'
              << "bounds_budget=" << options.budget << '\n'
              << "warmup_frames=" << options.warmup << '\n'
              << "measured_frames=" << options.frames << '\n'
              << "camera_eye=" << options.eye.x << ',' << options.eye.y << ',' << options.eye.z
              << "\ncamera_yaw=" << options.yaw << "\ncamera_pitch=" << options.pitch
              << "\ncamera_fov=" << options.fov << '\n';
    timing_metadata();
}

void completed_sample(std::vector<Sample>& samples,
                      const rendering::vulkan::Statistics& statistics) {
    const auto id = statistics.gpu_frame_count;
    if (id == 0) {
        return;
    }
    if (id > samples.size() || samples[id - 1].submitted_frame != id) {
        throw std::runtime_error("GPU completion does not match the submitted frame record");
    }
    auto& sample = samples[id - 1];
    sample.completed_frame = id;
    sample.gpu_total_ms = statistics.gpu_milliseconds;
    sample.gpu_prepare_ms = statistics.gpu_prepare_milliseconds;
    sample.gpu_render_ms = statistics.gpu_render_milliseconds;
    sample.gpu_passes = statistics.gpu_passes;
    if (statistics.render_passes_profiled) {
        double total = 0;
        for (const auto& pass : sample.gpu_passes) {
            if (!std::isfinite(pass.milliseconds) || pass.milliseconds < 0) {
                throw std::runtime_error("Invalid per-pass GPU duration");
            }
            total += pass.milliseconds;
        }
        if (sample.gpu_passes.size() != sample.render_passes ||
            std::abs(total - sample.gpu_render_ms) > 0.000001) {
            throw std::runtime_error("Per-pass timestamps do not cover the completed render");
        }
    }
}

void validate_frame(const Options& options, const rendering::vulkan::Statistics& statistics) {
    if (statistics.validation_errors != 0) {
        throw std::runtime_error("Vulkan validation reported an error");
    }
    if (statistics.width != options.width || statistics.height != options.height) {
        throw std::runtime_error("Actual render extent differs from the requested extent: " +
                                 std::to_string(statistics.width) + "x" +
                                 std::to_string(statistics.height));
    }
}

void move_camera(Harness& harness, const Options& options, std::size_t frame) {
    if (!options.motion || frame < options.warmup) {
        return;
    }
    const auto progress = double(frame - options.warmup) / options.frames;
    const auto wave = progress < 1.0 / 3.0 ? std::sin(progress * 18.84955592153876) : 0.0;
    const bool cut = progress >= 1.0 / 3.0 && progress < 2.0 / 3.0;
    const auto scale = double(options.size.x) / 64.0;
    auto pose = camera::Pose{options.eye, options.yaw, options.pitch};
    pose.position.x += cut ? scale * 2 : scale * wave;
    pose.position.z += cut ? scale * 2 : scale * wave * 0.5;
    pose.yaw += cut ? -0.22 : wave * 0.08;
    pose.pitch += cut ? -0.04 : wave * 0.04;
    require(harness.view.set_pose(pose));
}

void capture(Harness& harness, const Options& options, std::vector<Sample>& samples) {
    auto previous_dispatches = harness.renderer.statistics().world_compute_passes;
    for (auto& sample : samples) {
        const auto begin = Clock::now();
        harness.window.poll();
        if (harness.window.closed() || !take(harness.renderer.begin_frame())) {
            throw std::runtime_error(
                "Window closed, minimized or became out-of-date during capture");
        }
        const auto begun = Clock::now();
        const auto update = Clock::now();
        move_camera(harness, options, static_cast<std::size_t>(&sample - samples.data()));
        harness.draw(options.budget, true);
        const auto end = Clock::now();
        const auto statistics = harness.renderer.statistics();
        validate_frame(options, statistics);
        sample.submitted_frame = statistics.frame_count;
        sample.cpu_frame_ms = milliseconds(begin, end);
        sample.cpu_begin_ms = milliseconds(begin, begun);
        sample.cpu_update_submit_ms = milliseconds(update, end);
        sample.cpu_wait_ms = statistics.cpu_wait_milliseconds;
        sample.cpu_record_ms = statistics.cpu_record_milliseconds;
        sample.cpu_submit_ms = statistics.cpu_submit_milliseconds;
        sample.allocated_bytes = statistics.allocated_bytes;
        sample.entity_upload_bytes = statistics.entity_upload_bytes;
        sample.world_dispatches = statistics.world_compute_passes - previous_dispatches;
        sample.render_passes = statistics.passes;
        previous_dispatches = statistics.world_compute_passes;
        completed_sample(samples, statistics);
    }
}

void drain(Harness& harness, const Options& options, std::vector<Sample>& samples) {
    harness.window.poll();
    if (!take(harness.renderer.begin_frame())) {
        throw std::runtime_error("Cannot collect the last measured submission");
    }
    // Finish the already acquired ImGui/presentation frame outside the captured workload.
    harness.draw(0, false, !options.screenshot.empty());
    const auto statistics = harness.renderer.statistics();
    validate_frame(options, statistics);
    completed_sample(samples, statistics);
    if (!options.screenshot.empty()) {
        require(harness.renderer.screenshot(options.screenshot));
    }
    if (statistics.gpu_timestamps_supported) {
        for (const auto& sample : samples) {
            if (sample.completed_frame != sample.submitted_frame) {
                throw std::runtime_error("Missing GPU timing after the final submission drain");
            }
        }
    }
}

void write_samples(const Options& options, const std::vector<Sample>& samples) {
    std::cout << "frame,phase,submitted_frame,gpu_completed_frame,cpu_frame_ms,cpu_begin_ms,"
                 "cpu_update_submit_ms,gpu_total_ms,gpu_prepare_ms,gpu_render_ms,"
                 "gpu_allocated_bytes,entity_upload_bytes,render_passes,world_dispatches,"
                 "cpu_wait_ms,cpu_record_ms,cpu_submit_ms\n";
    std::cout << std::fixed << std::setprecision(6);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        std::cout << index << ',' << (index < options.warmup ? "warmup" : "measured") << ','
                  << sample.submitted_frame << ',' << sample.completed_frame << ','
                  << sample.cpu_frame_ms << ',' << sample.cpu_begin_ms << ','
                  << sample.cpu_update_submit_ms << ',';
        if (sample.completed_frame != 0) {
            std::cout << sample.gpu_total_ms << ',' << sample.gpu_prepare_ms << ','
                      << sample.gpu_render_ms;
        } else {
            std::cout << ",,";
        }
        std::cout << ',' << sample.allocated_bytes << ',' << sample.entity_upload_bytes << ','
                  << sample.render_passes << ',' << sample.world_dispatches << ','
                  << sample.cpu_wait_ms << ',' << sample.cpu_record_ms << ','
                  << sample.cpu_submit_ms << '\n';
    }
}

void write_passes(const Options& options, const std::vector<Sample>& samples) {
    if (options.pass_output.empty()) {
        return;
    }
    std::ofstream output(options.pass_output);
    output << "frame,phase,submitted_frame,gpu_completed_frame,pass,gpu_ms\n";
    output << std::fixed << std::setprecision(6);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        for (const auto& pass : sample.gpu_passes) {
            output << index << ',' << (index < options.warmup ? "warmup" : "measured") << ','
                   << sample.submitted_frame << ',' << sample.completed_frame << ',' << pass.entry
                   << ',' << pass.milliseconds << '\n';
        }
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot write per-pass timing output");
    }
}

void advance_edit_frame(Harness& harness, const Options& options, bool active) {
    harness.window.poll();
    if (!take(harness.renderer.begin_frame())) {
        throw std::runtime_error("Cannot acquire an edit benchmark frame");
    }
    harness.draw(active ? options.budget : 0, active);
    validate_frame(options, harness.renderer.statistics());
}

struct EditSample {
    double cpu_ms;
    double first_frame_cpu_ms;
    std::size_t chunks;
    std::size_t blocks;
    std::size_t voxels;
    rendering::vulkan::Statistics gpu;
};

EditSample measure_edit(Harness& harness, const Options& options, double radius, bool erase) {
    const auto start = Clock::now();
    engine::world::editing::apply_brush(harness.world, {engine::world::editing::Tool::sphere,
                                                        options.brush_center, radius, 1, erase});
    const auto edited = Clock::now();
    const auto changes = harness.world.view();
    EditSample sample{milliseconds(start, edited),   0,
                      changes.changed_chunks.size(), changes.changed_blocks.size(),
                      changes.changed_voxels.size(), {}};
    advance_edit_frame(harness, options, true);
    sample.first_frame_cpu_ms = milliseconds(edited, Clock::now());
    const auto submitted = harness.renderer.statistics().frame_count;
    advance_edit_frame(harness, options, false);
    sample.gpu = harness.renderer.statistics();
    if (sample.gpu.gpu_timestamps_supported && sample.gpu.gpu_frame_count != submitted) {
        throw std::runtime_error("Edit GPU timing belongs to another submission");
    }
    return sample;
}

void measure_edits(Harness& harness, const Options& options) {
    if (options.edit_output.empty()) {
        return;
    }
    std::ofstream output(options.edit_output);
    output << "radius,iteration,erase,cpu_ms,first_frame_cpu_ms,changed_chunks,changed_blocks,"
              "changed_voxels,gpu_prepare_ms,gpu_render_ms,gpu_total_ms\n";
    output << std::fixed << std::setprecision(6);
    for (const auto radius : {10., 32., 64., 128., 256.}) {
        for (unsigned iteration = 0; iteration < 6; ++iteration) {
            const auto sample = measure_edit(harness, options, radius, iteration % 2 != 0);
            output << radius << ',' << iteration << ',' << iteration % 2 << ',' << sample.cpu_ms
                   << ',' << sample.first_frame_cpu_ms << ',' << sample.chunks << ','
                   << sample.blocks << ',' << sample.voxels << ','
                   << sample.gpu.gpu_prepare_milliseconds << ','
                   << sample.gpu.gpu_render_milliseconds << ',' << sample.gpu.gpu_milliseconds
                   << '\n';
        }
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot write edit benchmark results");
    }
}

void run(const Options& options) {
    Harness harness(options);
    run_metadata(options, harness.renderer.statistics());
    harness.generate(options);
    std::vector<Sample> samples(options.frames + options.warmup);
    capture(harness, options, samples);
    drain(harness, options, samples);
    write_samples(options, samples);
    write_passes(options, samples);
    measure_edits(harness, options);
    std::cerr << "status=completed\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) {
            usage();
        } else {
            run(options);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "status=failed\nerror=" << error.what() << '\n';
        return 1;
    }
}
