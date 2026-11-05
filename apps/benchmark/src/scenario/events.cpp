#include "json.hpp"
#include <set>
#include <stdexcept>
namespace engine::app::benchmark {
namespace {
Event brush(const json::object& object) {
    keys(object, {"frame", "type", "tool", "center", "radius", "material", "erase"});
    Event result;
    const auto tool = text(object.at("tool"));
    if (tool == "sphere") {
        result.brush.tool = world::editing::Tool::sphere;
    } else if (tool == "cube") {
        result.brush.tool = world::editing::Tool::cube;
    } else if (tool == "paint") {
        result.brush.tool = world::editing::Tool::paint;
    } else {
        throw std::invalid_argument("Brush tool must be sphere, cube or paint");
    }
    result.brush.center = vector(object.at("center"));
    result.brush.radius = real(object.at("radius"), 0.001, world::editing::kMaximumBrushRadius);
    result.brush.material = static_cast<std::uint16_t>(integer(object.at("material"), 0, 65535));
    if (const auto* value = object.if_contains("erase")) {
        result.brush.erase = value->as_bool();
    }
    return result;
}
Event event(const json::object& object, const std::filesystem::path& directory) {
    const auto type = text(object.at("type"));
    if (type == "brush") {
        return brush(object);
    }
    Event result;
    if (type == "fill") {
        keys(object, {"frame", "type", "start", "material"});
        result.operation = Operation::fill;
        result.voxel = voxel(object.at("start"));
        result.material = static_cast<std::uint16_t>(integer(object.at("material"), 0, 65535));
    } else if (type == "stamp") {
        keys(object, {"frame", "type", "model", "origin"});
        result.operation = Operation::stamp;
        result.model = relative_file(object.at("model"), directory);
        result.voxel = voxel(object.at("origin"));
    } else if (type == "spawn") {
        keys(object, {"frame", "type", "model", "entity", "position"});
        result.operation = Operation::spawn;
        result.model = relative_file(object.at("model"), directory);
        result.entity = text(object.at("entity"));
        result.position = vector(object.at("position"));
    } else if (type == "move_entity") {
        keys(object, {"frame", "type", "entity", "position"});
        result.operation = Operation::move_entity;
        result.entity = text(object.at("entity"));
        result.position = vector(object.at("position"));
    } else if (type == "clear_entities") {
        keys(object, {"frame", "type"});
        result.operation = Operation::clear_entities;
    } else {
        throw std::invalid_argument("Unknown event type: " + type);
    }
    return result;
}
void entity_reference(const Event& event, std::set<std::string>& entities) {
    if (event.operation == Operation::spawn && !entities.insert(event.entity).second) {
        throw std::invalid_argument("Entity already exists: " + event.entity);
    }
    if (event.operation == Operation::move_entity && !entities.contains(event.entity)) {
        throw std::invalid_argument("Entity has not been spawned: " + event.entity);
    }
    if (event.operation == Operation::clear_entities) {
        entities.clear();
    }
}
} // namespace
std::vector<Event> parse_events(const json::array& values, const std::filesystem::path& directory,
                                unsigned frames) {
    std::vector<Event> result;
    std::set<std::string> entities;
    for (const auto& value : values) {
        const auto& object = value.as_object();
        auto parsed = event(object, directory);
        parsed.frame = integer(object.at("frame"), 0, frames - 1);
        if (!result.empty() && parsed.frame < result.back().frame) {
            throw std::invalid_argument("Events must be ordered by frame");
        }
        entity_reference(parsed, entities);
        result.push_back(std::move(parsed));
    }
    return result;
}
} // namespace engine::app::benchmark
