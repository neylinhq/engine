#include "../scenario/json.hpp"
#include "report.hpp"
#include <fstream>
#include <iomanip>
#include <locale>
namespace engine::app::benchmark {
namespace {
std::ofstream stream(const std::filesystem::path& path) {
    std::ofstream result(path, std::ios::binary | std::ios::trunc);
    result.imbue(std::locale::classic());
    result << std::setprecision(17);
    if (!result) {
        throw std::runtime_error("Cannot open sample output");
    }
    return result;
}
void close(std::ofstream& output) {
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot finish sample output");
    }
}
void frames(const Session& session) {
    auto file = stream(session.options.output / "frames.csv");
    file << "frame,simulation_seconds,submission,camera_x_m,camera_y_m,camera_z_m,yaw_rad,pitch_"
            "rad,events,world_revision,frame_wall_ms,edit_wall_ms,cpu_wait_ms,cpu_record_ms,cpu_"
            "submit_present_ms,gpu_ms,gpu_prepare_ms,gpu_render_ms,renderer_allocated_bytes\n";
    for (const auto& sample : session.results.frames) {
        const auto& pose = sample.camera;
        file << sample.frame << ',' << sample.frame * session.scenario.step_seconds << ','
             << sample.submission << ',' << pose.position.x << ',' << pose.position.y << ','
             << pose.position.z << ',' << pose.yaw << ',' << pose.pitch << ',' << sample.events
             << ',' << sample.revision << ',' << sample.wall_ms << ',' << sample.edit_ms << ','
             << sample.wait_ms << ',' << sample.record_ms << ',' << sample.submit_ms << ','
             << sample.gpu_ms.value() << ',';
        if (sample.gpu_prepare_ms) {
            file << *sample.gpu_prepare_ms;
        }
        file << ',';
        if (sample.gpu_render_ms) {
            file << *sample.gpu_render_ms;
        }
        file << ',' << sample.renderer_bytes << '\n';
    }
    close(file);
}
void events(const Session& session) {
    auto file = stream(session.options.output / "events.csv");
    file << "event_index,frame,operation,wall_ms,world_revision_before,world_revision_after,"
            "entities_after\n";
    for (const auto& sample : session.results.events) {
        const auto& operation = session.scenario.document.at("events")
                                    .as_array()[sample.index]
                                    .as_object()
                                    .at("type")
                                    .as_string();
        file << sample.index << ',' << sample.frame << ',' << operation << ',' << sample.wall_ms
             << ',' << sample.revision_before << ',' << sample.revision_after << ','
             << sample.entities_after << '\n';
    }
    close(file);
}
void passes(const Session& session) {
    auto file = stream(session.options.output / "passes.csv");
    file << "frame,submission,pass,gpu_ms\n";
    for (const auto& sample : session.results.frames) {
        for (const auto& pass : sample.passes) {
            file << sample.frame << ',' << sample.submission << ',' << pass.entry << ','
                 << pass.milliseconds << '\n';
        }
    }
    close(file);
}
} // namespace
json::object write_samples(const Session& session) {
    frames(session);
    events(session);
    passes(session);
    write_json(session.options.output / "memory.json", session.results.memory);
    json::object artifacts;
    for (const auto name : {"frames.csv", "events.csv", "passes.csv", "memory.json"}) {
        artifacts[name] = platform::process::file_sha256(session.options.output / name);
    }
    if (session.options.capture) {
        artifacts["final.png"] =
            platform::process::file_sha256(session.options.output / "final.png");
    }
    return artifacts;
}
} // namespace engine::app::benchmark
