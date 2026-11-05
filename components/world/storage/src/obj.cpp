#include "codec/binary.hpp"
#include "import.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace engine::world::storage {
namespace {

struct ObjMaterial {
    std::uint32_t color = 0xFFFFFFU;
    std::shared_ptr<const Image> texture;
};

struct ObjIndex {
    std::size_t vertex = 0;
    std::size_t coordinate = 0;
    bool has_coordinate = false;
};

struct ObjState {
    std::vector<model::Vec3> vertices;
    std::vector<TextureCoordinate> coordinates;
    std::map<std::string, ObjMaterial> materials;
    ObjMaterial current;
    std::vector<Triangle> triangles;
};

std::uint32_t color(model::Vec3 value) {
    const auto red =
        static_cast<unsigned>(std::clamp(static_cast<float>(value.x), 0.0F, 1.0F) * 255);
    const auto green =
        static_cast<unsigned>(std::clamp(static_cast<float>(value.y), 0.0F, 1.0F) * 255);
    const auto blue =
        static_cast<unsigned>(std::clamp(static_cast<float>(value.z), 0.0F, 1.0F) * 255);
    return (red << 16) | (green << 8) | blue;
}

void discard_transform(std::istringstream& input) {
    for (unsigned index = 0; index < 3; ++index) {
        const auto position = input.tellg();
        double ignored = 0;
        if (!(input >> ignored)) {
            input.clear();
            input.seekg(position);
            return;
        }
    }
}

std::string texture_path(std::istringstream& input) {
    std::string token;
    while (input >> std::quoted(token)) {
        if (token == "-s" || token == "-o" || token == "-t") {
            // The original converter reads these tinyobj options but samples
            // unmodified vertex UVs.
            discard_transform(input);
        } else if (!token.empty() && token.front() == '-') {
            std::string ignored;
            input >> ignored;
            if (token == "-mm") {
                input >> ignored;
            }
        } else {
            std::string rest;
            std::getline(input, rest);
            return token + rest;
        }
    }
    throw std::runtime_error("MTL map_Kd has no texture path");
}

void load_materials(ObjState& state, const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Cannot open OBJ material library: " + path_text(path));
    }
    ObjMaterial* current = nullptr;
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream input{line};
        std::string token;
        input >> token;
        if (token == "newmtl") {
            std::string name;
            input >> name;
            current = &state.materials[name];
        } else if (token == "Kd" && current != nullptr) {
            model::Vec3 value;
            if (input >> value.x >> value.y >> value.z) {
                current->color = color(value);
            }
        } else if (token == "map_Kd" && current != nullptr) {
            auto name = texture_path(input);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())) != 0) {
                name.pop_back();
            }
            std::ranges::replace(name, '\\', '/');
            current->texture = load_image(path.parent_path() / utf8_path(name));
        }
    }
}

std::size_t obj_index(const std::string& token, std::size_t count) {
    const auto value = std::stoll(token);
    const auto index = value > 0 ? value - 1 : static_cast<long long>(count) + value;
    if (value == 0 || index < 0 || static_cast<std::uint64_t>(index) >= count) {
        throw std::runtime_error("OBJ index outside declared arrays");
    }
    return static_cast<std::size_t>(index);
}

ObjIndex read_index(const std::string& token, const ObjState& state) {
    const auto slash = token.find('/');
    ObjIndex result;
    result.vertex = obj_index(token.substr(0, slash), state.vertices.size());
    if (slash != std::string::npos) {
        const auto next = token.find('/', slash + 1);
        const auto coordinate = token.substr(slash + 1, next - slash - 1);
        if (!coordinate.empty()) {
            result.coordinate = obj_index(coordinate, state.coordinates.size());
            result.has_coordinate = true;
        }
    }
    return result;
}

void append_triangle(ObjState& state, std::array<ObjIndex, 3> indices) {
    Triangle triangle;
    triangle.color = state.current.color;
    triangle.texture = state.current.texture;
    for (unsigned index = 0; index < 3; ++index) {
        const auto reference = indices[index];
        triangle.points[index] = state.vertices[reference.vertex];
        if (reference.has_coordinate) {
            triangle.coordinates[index] = state.coordinates[reference.coordinate];
        } else {
            triangle.texture.reset();
        }
    }
    state.triangles.push_back(std::move(triangle));
}

void read_face(ObjState& state, std::istringstream& input) {
    std::vector<ObjIndex> indices;
    std::string token;
    while (input >> token) {
        if (token.starts_with('#')) {
            break;
        }
        indices.push_back(read_index(token, state));
    }
    if (indices.size() < 3) {
        throw std::runtime_error("OBJ face requires at least three vertices");
    }
    for (std::size_t index = 1; index + 1 < indices.size(); ++index) {
        append_triangle(state, {indices[0], indices[index], indices[index + 1]});
    }
}

void read_vertex(ObjState& state, std::istringstream& input) {
    model::Vec3 vertex;
    if (!(input >> vertex.x >> vertex.y >> vertex.z)) {
        throw std::runtime_error("Invalid OBJ vertex");
    }
    state.vertices.push_back(vertex);
}

void parse_line(ObjState& state, const std::string& line, const std::filesystem::path& directory) {
    std::istringstream input{line};
    std::string token;
    input >> token;
    if (token == "v") {
        read_vertex(state, input);
    } else if (token == "vt") {
        TextureCoordinate coordinate;
        if (!(input >> coordinate.u >> coordinate.v)) {
            throw std::runtime_error("Invalid OBJ texture coordinate");
        }
        state.coordinates.push_back(coordinate);
    } else if (token == "f") {
        read_face(state, input);
    } else if (token == "mtllib") {
        std::string name;
        while (input >> std::quoted(name)) {
            load_materials(state, directory / utf8_path(name));
        }
    } else if (token == "usemtl") {
        std::string name;
        input >> name;
        const auto material = state.materials.find(name);
        state.current = material != state.materials.end() ? material->second : ObjMaterial{};
    }
}

} // namespace

std::vector<Triangle> read_obj(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Cannot open OBJ mesh: " + path_text(path));
    }
    ObjState state;
    std::string line;
    while (std::getline(stream, line)) {
        parse_line(state, line, path.parent_path());
    }
    return std::move(state.triangles);
}

} // namespace engine::world::storage
