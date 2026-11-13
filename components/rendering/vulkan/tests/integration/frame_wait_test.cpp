#include <engine/rendering/vulkan/renderer.hpp>

#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
namespace rendering = engine::rendering;
namespace model = engine::world::model;
namespace camera = engine::motion::camera;
namespace window = engine::platform::window;

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

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

model::Model fixture() {
    std::vector<model::Sample> samples;
    for (int z = 0; z < 8; ++z) {
        for (int x = 0; x < 8; ++x) {
            samples.push_back({{x, 0, z}, 1});
        }
    }
    std::vector<model::Material> materials(2);
    materials[1].color_base = {0.2, 0.6, 0.1};
    return model::build_model({8, 8, 8}, std::move(materials), samples);
}

struct Harness {
    model::World world;
    camera::Camera view = take(camera::Camera::create(64, {{12, 10, -10}, 0, -0.3}));
    window::Window window = take(window::Window::create({127, 95, "Frame wait validation", false}));
    rendering::vulkan::Renderer renderer;
    rendering::settings::Settings settings;
    bool deferred;

    explicit Harness(rendering::vulkan::Configuration configuration)
        : renderer(take(rendering::vulkan::Renderer::create(window, configuration))),
          deferred(configuration.defer_frame_wait) {
        settings.render.version = rendering::settings::RenderVersion::albedo;
        load();
        view.clear_changed();
    }

    void load() {
        auto mapped = world.prepare_generation(fixture());
        require(renderer.generate_world(world, mapped, {64, 64, 64}, 1));
        const auto source = world.add_model(fixture());
        const auto type = world.create_entity_type(source);
        static_cast<void>(world.spawn_entity(type, {24, 12, 24}));
    }

    void begin(const rendering::vulkan::Statistics& before) {
        window.poll();
        check(take(renderer.begin_frame()), "Frame wait test could not acquire an image");
        const auto begun = renderer.statistics();
        check(begun.frame_count == before.frame_count, "Beginning a frame submitted GPU work");
        if (begun.gpu_timestamps_supported) {
            const auto expected = deferred ? before.gpu_frame_count : before.frame_count;
            check(begun.gpu_frame_count == expected,
                  "Completion was collected on the wrong side of CPU preparation");
        }
        const auto duplicate = renderer.begin_frame();
        check(!duplicate && duplicate.error().find("already acquired") != std::string::npos,
              "Repeated begin_frame reused an outstanding acquire semaphore");
    }

    void cpu_preparation(unsigned frame, bool active, bool paused) {
        ImGui::SetNextWindowPos({2, 2}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({100, 65}, ImGuiCond_Always);
        ImGui::Begin("Frame wait ownership");
        ImGui::Text("CPU preparation %u", frame);
        ImGui::End();
        if (frame == 4) {
            // UI world replacement must drain the previous GPU frame before changing owners.
            load();
        }
        if (!active) {
            return;
        }
        const auto id = world.at({1, 0, 1});
        auto material = world.materials()[id];
        material.color_base.x = 0.1 + 0.01 * frame;
        world.set_material(id, std::move(material));
        world.set({2, 1, 2}, frame % 2 == 0 ? id : 0);
        world.commit_edits();
        world.set_entity_position(0, {24.0 + frame, 12, 24});
        world.update_entities(1.0 / 60.0, 0.3,
                              renderer.history_index(settings.render.version, paused));
    }

    rendering::vulkan::FrameSampling draw(unsigned frame, bool active, bool paused) {
        rendering::vulkan::FrameInput input;
        input.world = world.view();
        input.entities = &world.entity_gpu();
        input.camera = &view;
        input.settings = &settings;
        input.active = active;
        input.paused = paused;
        input.bounds_budget = 64;
        input.capture_scene = frame == 11;
        rendering::vulkan::FrameSampling sampling;
        require(renderer.draw(input, &sampling));
        if (active) {
            world.acknowledge_changes();
        }
        return sampling;
    }

    void frames() {
        rendering::vulkan::FrameSampling previous;
        for (unsigned frame = 0; frame < 12; ++frame) {
            const auto before = renderer.statistics();
            begin(before);
            const auto active = frame != 8;
            const auto paused = frame == 7;
            cpu_preparation(frame, active, paused);
            const auto history = renderer.history_index(settings.render.version, paused || !active);
            const auto sampling = draw(frame, active, paused);
            const auto after = renderer.statistics();
            check(after.frame_count == before.frame_count + 1, "Draw did not submit one frame");
            if (after.gpu_timestamps_supported) {
                check(after.gpu_frame_count == before.frame_count,
                      "Previous GPU timings were lost before command reuse");
            }
            check(renderer.history_index(settings.render.version, true) == history,
                  "Deferred completion changed sampling or entity history order");
            check(sampling.taa_index == history, "Captured sampling came from another frame");
            const auto advance = static_cast<unsigned>(active && !paused);
            check(sampling.shader_frame == previous.shader_frame + advance,
                  "Captured shader frame did not follow the sampling clock");
            if (advance == 0) {
                check(sampling.random_counter == previous.random_counter &&
                          sampling.random_counter_secondary == previous.random_counter_secondary,
                      "Paused sampling changed the captured random counters");
            }
            previous = sampling;
            check(after.validation_errors == 0, "Validation error during deferred resource reuse");
        }
    }

    void capture(const std::filesystem::path& path) {
        require(renderer.screenshot(path));
        const auto completed = renderer.statistics();
        if (completed.gpu_timestamps_supported) {
            check(completed.gpu_frame_count == completed.frame_count,
                  "Capture did not collect the last submitted frame");
        }
        check(completed.validation_errors == 0, "Validation error after completion drain");
        check(std::filesystem::file_size(path) > 50, "Explicit scene capture is empty");
    }
};

void run(const std::filesystem::path& shaders, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    for (const bool deferred : {false, true}) {
        for (const bool offscreen : {false, true}) {
            rendering::vulkan::Configuration configuration;
            configuration.shaders = shaders;
            configuration.validation = true;
            configuration.defer_frame_wait = deferred;
            configuration.offscreen_output = offscreen;
            Harness app(configuration);
            app.frames();
            const auto label = std::string(deferred ? "deferred" : "early") +
                               (offscreen ? "_offscreen" : "_automatic");
            app.capture(directory / (label + ".png"));
            const auto statistics = app.renderer.statistics();
            std::cout << label << ": submissions=" << statistics.frame_count
                      << ", completed=" << statistics.gpu_frame_count
                      << ", timestamps_supported=" << statistics.gpu_timestamps_supported
                      << ", validation_errors=" << statistics.validation_errors << '\n';
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error(
                "Usage: rendering_frame_wait_test SHADER_DIRECTORY OUTPUT_DIRECTORY");
        }
        run(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
