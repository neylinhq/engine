#include <boost/json.hpp>
#include <engine/platform/process/process.hpp>
#include <engine/world/model/types.hpp>
#include <engine/world/storage/storage.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
namespace engine::app::benchmark {
namespace {
namespace json = boost::json;
namespace process = engine::platform::process;
namespace model = engine::world::model;
void require(bool value, std::string_view message) {
    if (!value) {
        throw std::runtime_error(std::string(message));
    }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Missing test result");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
    require(bool(file), "Cannot write test input");
}
json::object scenario() {
    return json::parse(R"({"schema":"engine.benchmark.scenario.v1","name":"integration",
      "scene":{"file":"world.cvox","materials":"world.materials.json"},
      "voxel_size":1,"frames":16,"warmup_frames":2,
      "step_seconds":0.016666666666666666,"memory_interval":4,
      "render":{"width":256,"height":256,"mode":"base","fov":70,"bounces":3,"profile":"passes",
                "settings_file":"day.settings.json"},
      "camera":[{"frame":0,"position":[16,16,-32],"yaw":0,"pitch":0},
                {"frame":15,"position":[18,16,-32],"yaw":0,"pitch":0}],
      "events":[
        {"frame":0,"type":"brush","tool":"cube","center":[16,16,16],"radius":4,"material":1},
        {"frame":1,"type":"fill","start":[16,16,16],"material":2},
        {"frame":2,"type":"brush","tool":"paint","center":[16,16,16],"radius":1,"material":1},
        {"frame":3,"type":"brush","tool":"sphere","center":[16,16,16],"radius":0.9,"material":0,"erase":true},
        {"frame":4,"type":"stamp","model":"model.cvox","origin":[4,4,4]},
        {"frame":5,"type":"spawn","model":"model.cvox","entity":"one","position":[12,12,12]},
        {"frame":6,"type":"move_entity","entity":"one","position":[14,12,12]},
        {"frame":7,"type":"clear_entities"},
        {"frame":8,"type":"spawn","model":"model.cvox","entity":"two","position":[12,12,12]},
        {"frame":9,"type":"spawn","model":"model.cvox","entity":"three","position":[14,12,12]},
        {"frame":10,"type":"clear_entities"}],
      "probes":[{"position":[16,16,16],"material":0},{"position":[4,4,4],"material":3}]
    })")
        .as_object();
}
void assets(const std::filesystem::path& root) {
    const std::vector<model::Material> palette{{},
                                               {.name = "red", .color_base = {0.8, 0.2, 0.1}},
                                               {.name = "green", .color_base = {0.1, 0.8, 0.2}}};
    engine::world::storage::save_cvox(
        root / "world.cvox",
        model::build_model({32, 32, 32}, palette, {{{1, 1, 1}, 1}, {{2, 1, 1}, 2}}));
    engine::world::storage::save_cvox(root / "model.cvox",
                                      model::build_model({16, 16, 16}, palette, {{{0, 0, 0}, 2}}));
    write(root / "world.materials.json",
          R"({"schema":"engine.materials.v1","materials":[
            {"id":2,"name":"green","color_base":[0.1,0.8,0.2],"color_layer":[0,0,0],
             "base":"diffuse","layer":"none","roughness":0},
            {"id":0,"name":"","color_base":[0,0,0],"color_layer":[0,0,0],
             "base":"diffuse","layer":"none","roughness":0},
            {"id":1,"name":"red","color_base":[0,0,1],"color_layer":[0,0,0],
             "base":"diffuse","layer":"none","roughness":0}
          ]})");
    write(root / "day.settings.json", R"({"general":{"fov":60,"exposure":2}})");
    write(root / "case.json", json::serialize(scenario()));
}
void check_result(const std::filesystem::path& directory, std::set<std::uint64_t>& processes) {
    const auto report = json::parse(read(directory / "summary.json")).as_object();
    require(report.at("status") == "complete", "Incomplete run");
    const auto& coverage = report.at("coverage").as_object();
    require(coverage.at("gpu_timed_frames").to_number<unsigned>() == 16,
            "GPU frame coverage mismatch");
    require(coverage.at("recorded_events").to_number<unsigned>() == 11, "Event coverage mismatch");
    require(coverage.at("prepared_entity_types").to_number<unsigned>() == 1,
            "Repeated instances duplicated their entity type");
    require(coverage.at("validated_probes").to_number<unsigned>() == 2, "Voxel probes not checked");
    require(report.at("validation_errors").to_number<unsigned>() == 0, "Vulkan validation errors");
    const auto& workload = report.at("workload").as_object();
    const auto& red = workload.at("world_palette").as_array().at(2).as_object();
    require(red.at("name") == "red" &&
                red.at("color_base").as_array() == json::array{0.0, 0.0, 1.0},
            "External palette did not replace the source material");
    const auto& settings = workload.at("settings").as_object().at("general").as_object();
    require(settings.at("exposure").to_number<double>() == 2 &&
                settings.at("fov").to_number<double>() == 70,
            "External settings or explicit scenario override did not apply");
    processes.insert(report.at("process_id").to_number<std::uint64_t>());
    const auto memory = json::parse(read(directory / "memory.json")).as_array();
    require(memory.size() == 7, "Memory samples missing");
    require(memory.front().as_object().at("resident_bytes").to_number<std::uint64_t>() > 0,
            "Missing RSS");
    require(read(directory / "frames.csv").find("15,0.25,18,") != std::string::npos,
            "Last measured submission not recorded");
    require(read(directory / "passes.csv").find("base_renderFinal_MainPS") != std::string::npos,
            "Pass profile missing");
}
void comparison_checks(const std::filesystem::path& executable,
                       const std::filesystem::path& output) {
    const auto path = process::path_text(output);
    const auto command =
        process::Command{executable, {"compare", "--baseline", path, "--candidate", path}};
    require(process::run(command) == 0, "Self-comparison failed");
    const auto frames = output / "run_000/frames.csv";
    const auto original = read(frames);
    write(frames, original + "corrupted\n");
    const auto code = process::run(command);
    write(frames, original);
    require(code == 4, "Corrupted artifact was accepted");
}
void changed_workload(const std::filesystem::path& executable, const std::filesystem::path& root) {
    auto changed = scenario();
    changed["warmup_frames"] = 0;
    write(root / "changed.json", json::serialize(changed));
    const auto candidate = root / "candidate";
    const auto command =
        process::Command{executable,
                         {"measure", "--scenario", process::path_text(root / "changed.json"),
                          "--output", process::path_text(candidate), "--hidden", "--validation"}};
    require(process::run(command) == 0, "Zero-warmup scenario failed");
    require(read(candidate / "frames.csv").find("15,0.25,16,") != std::string::npos,
            "Zero-warmup final submission missing");
    require(process::run({executable,
                          {"compare", "--baseline", process::path_text(root / "results"),
                           "--candidate", process::path_text(candidate)}}) == 4,
            "Incompatible workloads were compared");
    changed["probes"].as_array()[1].as_object()["material"] = 2;
    write(root / "wrong_probe.json", json::serialize(changed));
    const auto failed = root / "failed";
    require(process::run({executable,
                          {"measure", "--scenario", process::path_text(root / "wrong_probe.json"),
                           "--output", process::path_text(failed), "--hidden", "--validation"}}) ==
                3,
            "Incorrect world result was accepted");
    require(std::filesystem::exists(failed / "failure.json") &&
                !std::filesystem::exists(failed / "summary.json"),
            "Failed worker marked complete");
}
void path_scene(const std::filesystem::path& root) {
    std::vector<model::Sample> samples;
    for (int z = 0; z < 32; ++z) {
        for (int x = 0; x < 32; ++x) {
            samples.push_back({{x, 8, z}, 1});
        }
    }
    engine::world::storage::save_cvox(
        root / "path_world.cvox",
        model::build_model({32, 32, 32}, {{}, {.name = {}, .color_base = {0.6, 0.3, 0.1}}},
                           samples));
}
void path_accumulation(const std::filesystem::path& executable, const std::filesystem::path& root) {
    path_scene(root);
    auto value = scenario();
    value["scene"] = json::object{{"file", "path_world.cvox"}};
    value["camera"] = json::array{json::object{
        {"frame", 0}, {"position", json::array{16, 20, -24}}, {"yaw", 0}, {"pitch", -0.25}}};
    value.erase("events");
    value.erase("probes");
    auto& render = value["render"].as_object();
    render["mode"] = "path_tracer";
    render.erase("settings_file");
    std::vector<std::string> images;
    for (const auto samples : {1, 16}) {
        const auto name = "path_samples_" + std::to_string(samples);
        render["settings"] = json::object{
            {"render", json::object{{"renderPathTracer", json::object{{"maxSamples", samples},
                                                                      {"isTAAJitter", true}}}}}};
        const auto input = root / (name + ".json");
        const auto output = root / name;
        write(input, json::serialize(value));
        require(process::run(
                    {executable,
                     {"measure", "--scenario", process::path_text(input), "--output",
                      process::path_text(output), "--hidden", "--validation", "--capture"}}) == 0,
                "Static PathTracer scenario failed");
        images.push_back(read(output / "final.png"));
    }
    require(images.front() != images.back(), "Static PathTracer ignored the accumulation limit");
}
void run(const std::vector<std::string>& arguments) {
    require(arguments.size() == 4, "Expected executable, shaders and scratch directory");
    const auto executable = process::text_path(arguments[1]);
    const auto root =
        process::text_path(arguments[3]) /
        process::text_path("case Юникод " + std::to_string(process::identity().process_id));
    require(std::filesystem::create_directories(root), "Scratch directory already exists");
    assets(root);
    const auto input = process::path_text(root / "case.json");
    require(process::run({executable, {"validate", "--scenario", input}}) == 0,
            "Scenario with external inputs did not validate");
    const auto invalid = root / "duplicate.materials.json";
    write(invalid, R"({"schema":"engine.materials.v1","materials":[
      {"id":0,"name":"","color_base":[0,0,0],"color_layer":[0,0,0],
       "base":"diffuse","layer":"none","roughness":0},
      {"id":0}, {"id":2}
    ]})");
    require(process::run({executable,
                          {"validate", "--scenario", input, "--materials",
                           process::path_text(invalid)}}) == 2,
            "Duplicate local material ID was accepted");
    const auto output = root / "results";
    const auto command =
        process::Command{executable,
                         {"run", "--scenario", process::path_text(root / "case.json"), "--shaders",
                          arguments[2], "--output", process::path_text(output), "--repetitions",
                          "2", "--hidden", "--validation", "--capture"}};
    require(process::run(command) == 0, "Scripted benchmark failed");
    std::set<std::uint64_t> processes;
    check_result(output / "run_000", processes);
    check_result(output / "run_001", processes);
    require(processes.size() == 2 && !processes.contains(process::identity().process_id),
            "Runs were not isolated processes");
    const auto before = process::file_sha256(output / "suite.json");
    require(process::run(command) == 2, "Existing results were overwritten");
    require(before == process::file_sha256(output / "suite.json"), "Existing summary changed");
    comparison_checks(executable, output);
    changed_workload(executable, root);
    path_accumulation(executable, root);
}
int checked(const std::vector<std::string>& arguments) {
    try {
        run(arguments);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
} // namespace
} // namespace engine::app::benchmark
#ifdef _WIN32
int wmain(int count, wchar_t** values) {
    std::vector<std::string> arguments;
    for (int index = 0; index < count; ++index) {
        arguments.push_back(engine::platform::process::path_text(values[index]));
    }
    return engine::app::benchmark::checked(arguments);
}
#else
int main(int count, char** values) {
    return engine::app::benchmark::checked(std::vector<std::string>(values, values + count));
}
#endif
