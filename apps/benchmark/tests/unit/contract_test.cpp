#include "../../src/report/report.hpp"
#include "../../src/scenario/json.hpp"
#include <cmath>
#include <iostream>
namespace {
using namespace engine::app::benchmark;
void require(bool value) {
    if (!value) {
        throw std::runtime_error("Benchmark contract failed");
    }
}
json::object document() {
    return json::parse(R"({"schema":"engine.benchmark.scenario.v1","name":"test",
      "scene":{"size":[32,32,32]},"voxel_size":1,"frames":11,"warmup_frames":2,
      "step_seconds":0.016666666666666666,"memory_interval":5,
      "render":{"width":64,"height":64,"mode":"base","fov":70,"bounces":3},
      "camera":[{"frame":0,"position":[0,0,-10],"yaw":0,"pitch":0},
                {"frame":10,"position":[10,20,-10],"yaw":1,"pitch":0.5}],
      "events":[{"frame":2,"type":"brush","tool":"sphere","center":[16,16,16],"radius":3,"material":1}]
    })")
        .as_object();
}
void rejected(const json::object& value) {
    bool failed = false;
    try {
        static_cast<void>(parse_scenario(value, std::filesystem::current_path()));
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed);
}
void scenarios() {
    const auto scenario = parse_scenario(document(), std::filesystem::current_path());
    const auto middle = camera_at(scenario.camera, 5);
    require(middle.position.x == 5 && middle.position.y == 10 && middle.yaw == 0.5 &&
            middle.pitch == 0.25);
    require(camera_at(scenario.camera, 10).position.x == 10);
    require(scenario.events.front().frame == 2 && scenario.settings.general.fov == 70);
    auto bad = document();
    bad["frmaes"] = 100;
    rejected(bad);
    bad = document();
    bad["frames"] = 10.5;
    rejected(bad);
    bad = document();
    bad["schema"] = "engine.benchmark.scenario.v2";
    rejected(bad);
    bad = document();
    bad["camera"].as_array()[0].as_object()["frame"] = 1;
    rejected(bad);
    bad = document();
    bad["events"].as_array()[0].as_object()["frame"] = 11;
    rejected(bad);
    bad = document();
    bad["render"].as_object()["mode"] = "unknown";
    rejected(bad);
    bad = document();
    bad["render"].as_object()["bounces"] = 11;
    rejected(bad);
    bad = document();
    bad["scene"].as_object()["file"] = "unexpected.cvox";
    rejected(bad);
    bad = document();
    bad["events"] = json::array{json::object{{"frame", 0},
                                             {"type", "move_entity"},
                                             {"entity", "missing"},
                                             {"position", json::array{0, 0, 0}}}};
    rejected(bad);
}
void statistics() {
    std::vector<double> values;
    for (unsigned index = 1; index <= 100; ++index) {
        values.push_back(index);
    }
    const auto stats = distribution(values).as_object();
    require(std::abs(stats.at("p50").to_number<double>() - 50.5) < 1e-12);
    require(std::abs(stats.at("p99").to_number<double>() - 99.01) < 1e-12);
    require(distribution({}).is_null());
    const std::vector<double> baseline{10, 10, 10}, candidate{8, 8, 8};
    const auto change = effect(baseline, candidate);
    require(std::abs(change.at("ratio").to_number<double>() - 0.8) < 1e-12);
    require(change.at("ci95").as_array()[0].to_number<double>() == 0.8);
    require(effect(std::span(baseline).first(1), candidate).at("ci95").is_null());
    require(fingerprint(json::object{{"b", 2}, {"a", 1}}) ==
            fingerprint(json::object{{"a", 1}, {"b", 2}}));
}
void camera_limits() {
    for (const auto pitch : {-1.55, 1.55}) {
        auto value = document();
        value["camera"].as_array()[0].as_object()["pitch"] = pitch;
        const auto scenario = parse_scenario(value, std::filesystem::current_path());
        const auto camera =
            engine::motion::camera::Camera::create(32, scenario.camera.front().pose).value();
        require(camera.pose().pitch == pitch);
    }
    for (const auto pitch : {-1.56, 1.56, std::nextafter(-1.55, -2.0), std::nextafter(1.55, 2.0)}) {
        auto value = document();
        value["camera"].as_array()[0].as_object()["pitch"] = pitch;
        rejected(value);
    }
}
void brush_limits() {
    for (const auto radius : {0.001, 400.0}) {
        auto value = document();
        value["events"].as_array()[0].as_object()["radius"] = radius;
        const auto scenario = parse_scenario(value, std::filesystem::current_path());
        require(scenario.events.front().brush.radius == radius);
    }
    for (const auto radius :
         {0.0, std::nextafter(0.001, 0.0), std::nextafter(400.0, 401.0), 401.0}) {
        auto value = document();
        value["events"].as_array()[0].as_object()["radius"] = radius;
        rejected(value);
    }
}
} // namespace
int main() {
    try {
        scenarios();
        statistics();
        camera_limits();
        brush_limits();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
