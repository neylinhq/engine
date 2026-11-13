#include <engine/world/storage/storage.hpp>

#include "codec/binary.hpp"

#include <engine/platform/filesystem/filesystem.hpp>

#include <cmath>
#include <stdexcept>

namespace engine::world::storage {
namespace {

void validate_node(const PathNode& node) {
    if (!foundation::geometry::finite(node.position) ||
        !foundation::geometry::finite(node.direction) ||
        foundation::geometry::length(node.direction) == 0 || !std::isfinite(node.speed) ||
        node.speed <= 0) {
        throw std::runtime_error("Invalid camera path position, direction or speed");
    }
}

PathNode read_node(Reader& reader) {
    PathNode result;
    result.position = {reader.f32(), reader.f32(), reader.f32()};
    result.direction = {reader.f32(), reader.f32(), reader.f32()};
    result.speed = reader.f32();
    result.name = reader.terminated();
    validate_node(result);
    return result;
}

void write_node(Writer& writer, const PathNode& node) {
    validate_node(node);
    writer.f32(static_cast<float>(node.position.x));
    writer.f32(static_cast<float>(node.position.y));
    writer.f32(static_cast<float>(node.position.z));
    writer.f32(static_cast<float>(node.direction.x));
    writer.f32(static_cast<float>(node.direction.y));
    writer.f32(static_cast<float>(node.direction.z));
    writer.f32(node.speed);
    writer.terminated(node.name);
}

} // namespace

std::vector<PathNode> load_path(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    Reader reader{bytes};
    if (reader.u32() != 1) {
        throw std::runtime_error("Unsupported PNB camera path version");
    }
    const auto count = reader.u32();
    if (count > reader.remaining() / 29) {
        throw std::runtime_error("Truncated PNB camera path");
    }
    std::vector<PathNode> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        result.push_back(read_node(reader));
    }
    if (reader.remaining() != 0) {
        throw std::runtime_error("Unexpected trailing PNB camera path bytes");
    }
    return result;
}

void save_path(const std::filesystem::path& path, std::span<const PathNode> nodes) {
    if (nodes.size() > 0x7FFFFFFFU) {
        throw std::runtime_error("Too many PNB camera path nodes");
    }
    Writer writer;
    writer.u32(1);
    writer.u32(static_cast<std::uint32_t>(nodes.size()));
    for (const auto& node : nodes) {
        write_node(writer, node);
    }
    platform::filesystem::write(path, writer.bytes());
}

} // namespace engine::world::storage
