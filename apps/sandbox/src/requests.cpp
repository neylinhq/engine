#include "application.hpp"
#include "io/path.hpp"
#include <iostream>
#include <stdexcept>
namespace engine::app::sandbox {
namespace {
void replace_world(Application& app, const gui::frontend::Request& request,
                   world::model::Model model) {
    const auto fixed = request.operation == gui::frontend::Operation::open_fixed;
    generate_world(app, std::move(model), fixed ? request.requested_size : world::model::IVec3{},
                   request.segment_groups);
    app.ui->reset_world();
    reframe_camera(app);
    app.metrics.status = "Loaded " + path_to_utf8(request.path);
}
void begin_import(Application& app, const gui::frontend::Request& request) {
    if (app.importing) {
        throw std::runtime_error("Another file import is still running");
    }
    auto task = std::async(std::launch::async, [request] {
        return world::storage::load(request.path, request.import_options);
    });
    app.importing = PendingImport{request, std::move(task)};
    app.metrics.status = "Loading " + path_to_utf8(request.path);
}
void handle_request(Application& app, const gui::frontend::Request& request) {
    using gui::frontend::Operation;
    switch (request.operation) {
    case Operation::save_world:
        world::storage::save_cvox(request.path, app.world.snapshot());
        app.metrics.status = "Saved " + path_to_utf8(request.path);
        break;
    case Operation::import_model:
        begin_import(app, request);
        break;
    case Operation::save_settings:
        save_settings(app);
        app.metrics.status = "Settings saved";
        break;
    case Operation::restart_render:
        app.renderer->restart();
        break;
    case Operation::load_path:
        load_path(app, request.path);
        break;
    case Operation::save_path:
        save_path(app, request.path);
        app.metrics.status = "Saved path " + path_to_utf8(request.path);
        break;
    case Operation::screenshot:
        app.screenshot_requested = true;
        break;
    default:
        begin_import(app, request);
        break;
    }
}
} // namespace
void generate_world(Application& app, world::model::Model source, world::model::IVec3 requested,
                    int segment_groups) {
    auto mapped = app.world.prepare_generation(std::move(source));
    if (requested == world::model::IVec3{}) {
        requested = mapped.size;
    }
    const auto result = app.renderer->generate_world(app.world, mapped, requested, segment_groups);
    if (!result) {
        throw std::runtime_error(result.error());
    }
}
void poll_import(Application& app) {
    if (!app.importing ||
        app.importing->task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    auto pending = std::move(*app.importing);
    app.importing.reset();
    try {
        auto model = pending.task.get();
        if (pending.request.operation == gui::frontend::Operation::import_model) {
            static_cast<void>(app.world.add_model(std::move(model)));
            app.metrics.status = "Imported model " + path_to_utf8(pending.request.path);
        } else {
            replace_world(app, pending.request, std::move(model));
        }
    } catch (const std::exception& error) {
        app.metrics.status = "Import failed: " + std::string(error.what());
    }
}
void handle_requests(Application& app, const std::vector<gui::frontend::Request>& requests) {
    for (const auto& request : requests) {
        try {
            handle_request(app, request);
        } catch (const std::exception& error) {
            app.metrics.status = error.what();
            std::cerr << "NAADF operation: " << error.what() << '\n';
        }
    }
}
void load_path(Application& app, const std::filesystem::path& path) {
    const auto stored = world::storage::load_path(path);
    std::vector<motion::path::Node> nodes;
    nodes.reserve(stored.size());
    for (const auto& node : stored) {
        nodes.push_back({node.position, node.direction, 1.0 / node.speed, node.name});
    }
    if (const auto result = app.path.replace(std::move(nodes)); !result) {
        throw std::runtime_error(result.error());
    }
    app.metrics.status = "Loaded path " + path_to_utf8(path);
}
void save_path(const Application& app, const std::filesystem::path& path) {
    std::vector<world::storage::PathNode> nodes;
    nodes.reserve(app.path.nodes().size());
    for (const auto& node : app.path.nodes()) {
        nodes.push_back(
            {node.position, node.direction, static_cast<float>(1.0 / node.duration), node.name});
    }
    world::storage::save_path(path, nodes);
}
} // namespace engine::app::sandbox
