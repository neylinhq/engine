#include <engine/rendering/vulkan/renderer.hpp>
#include <engine/world/editing/editing.hpp>

#include <fstream>
#include <imgui.h>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
namespace rendering = engine::rendering;
namespace model = engine::world::model;
namespace camera = engine::motion::camera;
namespace window = engine::platform::window;
namespace editing = engine::world::editing;

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

std::vector<char> read_image(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot read captured rollback image");
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::uint64_t generation_dispatches(const rendering::vulkan::Renderer& renderer) {
    for (const auto& execution : renderer.world_executions()) {
        if (execution.entry == "world_generator_generatorModel_fillChunkDataWithModelData16") {
            return execution.dispatches;
        }
    }
    throw std::runtime_error("Original GPU model generation was never dispatched");
}

model::Model fixture() {
    std::vector<model::Sample> samples;
    for (int z = 0; z < 24; ++z) {
        for (int x = 0; x < 24; ++x) {
            samples.push_back({{x, 0, z}, 1});
        }
    }
    for (int y = 1; y < 12; ++y) {
        samples.push_back({{12, y, 12}, 2});
    }
    std::vector<model::Material> materials(3);
    materials[1].color_base = {0.2, 0.6, 0.1};
    materials[2].color_base = {0.8, 0.1, 0.05};
    return model::build_model({24, 12, 24}, std::move(materials), samples);
}

struct Harness {
    model::World world;
    camera::Camera view = take(camera::Camera::create(128, {{12, 26, -25}, 0, -0.4}));
    window::Window window =
        take(window::Window::create({127, 95, "NAADF Vulkan validation", false}));
    rendering::vulkan::Renderer renderer;
    rendering::settings::Settings settings;
    bool active = true;
    bool paused = false;
    unsigned bounds_budget = 1024;

    explicit Harness(const std::filesystem::path& shaders)
        : renderer(take(rendering::vulkan::Renderer::create(window, {shaders, true, false}))) {
        load(fixture(), {128, 128, 128}, true);
        view.clear_changed();
    }

    void load(model::Model source, model::IVec3 requested, bool return_mirror = false) {
        auto mapped = world.prepare_generation(std::move(source));
        if (return_mirror) {
            world.adopt_generated(take(renderer.generate_world(mapped, requested, 1)));
        } else {
            require(renderer.generate_world(world, mapped, requested, 1));
        }
        if (!world.view().gpu_authoritative) {
            throw std::runtime_error("Desktop world load bypassed GPU generation");
        }
    }

    std::vector<char> scene(const std::filesystem::path& path) {
        frames(2, true);
        require(renderer.screenshot(path));
        return read_image(path);
    }

    void rollback_edit(std::uint16_t material) {
        editing::apply_brush(world, {editing::Tool::cube, {100, 2, 100}, 1, material, false});
        frames(2);
        if (world.at({100, 2, 100}) != material) {
            throw std::runtime_error("Edits no longer target the restored old world");
        }
    }

    void rollback(const std::filesystem::path& directory) {
        const auto previous = world.view();
        const auto material = world.at({100, 0, 100});
        const auto dispatches = generation_dispatches(renderer);
        auto pose = view.pose();
        pose.position = {100, 4.5, 90};
        pose.yaw = 0;
        pose.pitch = -0.2;
        require(view.set_pose(pose));
        settings.render.version = rendering::settings::RenderVersion::albedo;
        const auto before = scene(directory / "rollback_before.png");
        model::World foreign;
        const auto mapped = foreign.prepare_generation(fixture());
        const auto result = renderer.generate_world(world, mapped, {64, 64, 64}, 1);
        if (result || result.error().find("another material registry") == std::string::npos ||
            generation_dispatches(renderer) <= dispatches) {
            throw std::runtime_error("Foreign mirror was not rejected after real GPU generation");
        }
        if (world.size() != previous.size || world.actual_size() != previous.actual_size ||
            world.view().revision != previous.revision || world.at({100, 0, 100}) != material) {
            throw std::runtime_error("Rejected adoption modified the previous CPU world");
        }
        if (scene(directory / "rollback_after.png") != before) {
            throw std::runtime_error("Rejected adoption failed to restore the resident GPU scene");
        }
        rollback_edit(material);
    }

    void frames(unsigned count, bool capture = false, bool capture_output = false) {
        for (unsigned frame = 0; frame < count;) {
            window.poll();
            if (!take(renderer.begin_frame())) {
                continue;
            }
            ImGui::SetNextWindowPos({2, 2}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({100, 65}, ImGuiCond_Always);
            ImGui::Begin("Lifecycle validation");
            ImGui::Text("Frame %u", frame);
            ImGui::End();
            const auto history_index =
                renderer.history_index(settings.render.version, paused || !active);
            if (active) {
                world.update_entities(1.0 / 60.0, 0.3, history_index);
            }
            rendering::vulkan::FrameInput input;
            input.world = world.view();
            input.entities = &world.entity_gpu();
            input.camera = &view;
            input.settings = &settings;
            input.capture_scene = capture;
            input.capture_output = capture_output && frame + 1 == count;
            input.active = active;
            input.paused = paused;
            input.bounds_budget = bounds_budget;
            require(renderer.draw(input));
            if (renderer.history_index(settings.render.version, true) != history_index) {
                throw std::runtime_error("Entity and renderer history indices diverged");
            }
            if (active) {
                world.acknowledge_changes();
            }
            if (renderer.statistics().validation_errors) {
                throw std::runtime_error("Vulkan validation reported an error");
            }
            ++frame;
        }
    }

    void select_mode(rendering::settings::RenderVersion mode) {
        settings.render.version = mode;
        if (renderer.history_index(mode, true) != 127 || renderer.history_index(mode) != 126) {
            throw std::runtime_error("A new render mode did not restart its sampling counter");
        }
        paused = true;
        frames(1);
        if (renderer.history_index(mode, true) != 127) {
            throw std::runtime_error("P changed the new renderer's initial history index");
        }
        paused = false;
    }

    void inactive_window() {
        const auto before = renderer.statistics();
        const auto history_index = renderer.history_index(settings.render.version, true);
        active = false;
        frames(2);
        const auto inactive = renderer.statistics();
        if (renderer.history_index(settings.render.version, true) != history_index ||
            inactive.world_compute_passes != before.world_compute_passes ||
            inactive.frame_count != before.frame_count + 2 || inactive.passes == 0) {
            throw std::runtime_error("Inactive window did not render with frozen world updates");
        }
        active = true;
        paused = true;
        frames(1);
        const auto sampling_paused = renderer.statistics();
        if (renderer.history_index(settings.render.version, true) != history_index ||
            sampling_paused.world_compute_passes <= inactive.world_compute_passes) {
            throw std::runtime_error("P pause incorrectly suspended the world update");
        }
        paused = false;
        frames(1);
        if (renderer.history_index(settings.render.version, true) == history_index) {
            throw std::runtime_error("Refocusing did not resume the sampling clock");
        }
    }

    void restart_mode() {
        const auto mode = settings.render.version;
        renderer.restart();
        if (renderer.history_index(mode, true) != 127 || renderer.history_index(mode) != 126) {
            throw std::runtime_error("Same-mode restart did not reset the predicted history slot");
        }
        paused = true;
        frames(1);
        if (renderer.history_index(mode, true) != 127) {
            throw std::runtime_error("Same-mode restart ignored P on its first frame");
        }
        paused = false;
        frames(2);
        if (renderer.history_index(mode, true) != 125) {
            throw std::runtime_error("A completed same-mode restart repeated on later frames");
        }
    }

    void edits() {
        const auto material = world.at({4, 0, 4});
        editing::apply_brush(world, {editing::Tool::sphere, {4, 4, 4}, 2, material, false});
        frames(2);
        auto paint = world.materials()[material];
        paint.name = "Smoke paint";
        paint.color_base = {0.1, 0.2, 0.9};
        const auto paint_index = world.add_material(std::move(paint));
        editing::apply_brush(world, {editing::Tool::paint, {4, 4, 4}, 2, paint_index, false});
        paused = true;
        bounds_budget = 0;
        frames(2);
        paused = false;
        bounds_budget = 1024;
        editing::flood_fill(world, {4, 4, 4}, material);
        editing::apply_brush(world, {editing::Tool::cube, {4, 4, 4}, 1, material, true});
        frames(2);
        const auto source = world.add_model(fixture());
        editing::stamp_model(world, world.models()[source], {40, 4, 40});
        frames(2);
        if (world.at({4, 4, 4}) != 0 || world.at({40, 4, 40}) == 0) {
            throw std::runtime_error("Original edit commands lost their CPU query mirror");
        }
    }

    void replacement() {
        const auto library_size = world.models().size();
        load(fixture(), {48, 24, 48});
        frames(2);
        load(fixture(), {128, 128, 128});
        frames(2);
        if (world.models().size() != library_size || !world.entity_types().empty()) {
            throw std::runtime_error("World replacement lost library or retained old entities");
        }
    }

    void entities() {
        const auto index = world.add_model(fixture());
        const auto type = world.create_entity_type(index);
        static_cast<void>(world.spawn_entity(type, {25, 15, 25}));
        frames(4);
        world.set_entity_position(0, {70, 20, 65});
        frames(4);
        world.clear_entities();
        frames(2);
    }

    void resize() {
        settings.render.version = rendering::settings::RenderVersion::albedo;
        require(window.toggle_borderless());
        frames(2);
        require(window.toggle_borderless());
        frames(2);
        if (window.borderless()) {
            throw std::runtime_error("F11 did not restore the window state");
        }
    }
};

void run(const std::filesystem::path& shaders, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    Harness app(shaders);
    for (const auto mode :
         {rendering::settings::RenderVersion::albedo, rendering::settings::RenderVersion::base,
          rendering::settings::RenderVersion::path_tracer}) {
        app.select_mode(mode);
        app.frames(8, false, true);
        require(app.renderer.screenshot(directory / (std::to_string(int(mode)) + "_ui.png")));
        app.restart_mode();
        app.settings.render.show_steps = true;
        app.frames(1);
        app.settings.render.show_steps = false;
        app.frames(1);
    }
    app.settings.render.version = rendering::settings::RenderVersion::base;
    app.entities();
    app.inactive_window();
    app.edits();
    const auto floor_material = app.world.at({4, 0, 4});
    auto material = app.world.materials()[floor_material];
    material.color_base = {0.15, 0.3, 0.8};
    app.world.set_material(floor_material, material);
    app.frames(2);
    app.frames(1, true);
    require(app.renderer.screenshot(directory / "scene_only.png"));
    app.resize();
    app.replacement();
    app.rollback(directory);
    std::cout
        << "Vulkan lifecycle passed: GPU generation/replacement/rollback, brush/paint/fill/stamp, "
           "paused edits, inactive windows, mode restarts, entities, history, ImGui, scene capture "
           "and "
           "F11\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error(
                "Usage: rendering_lifecycle_test SHADER_DIRECTORY OUTPUT_DIRECTORY");
        }
        run(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
