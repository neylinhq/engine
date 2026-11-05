#include "import.hpp"

#include "codec/binary.hpp"

#include <sstream>
#include <stdexcept>

namespace engine::world::storage {
namespace {

std::vector<Triangle> binary_stl(Reader& reader, std::uint32_t count) {
    std::vector<Triangle> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        static_cast<void>(reader.take(12));
        Triangle triangle;
        for (auto& point : triangle.points) {
            point = {reader.f32(), reader.f32(), reader.f32()};
        }
        static_cast<void>(reader.u16());
        result.push_back(std::move(triangle));
    }
    return result;
}

std::vector<Triangle> ascii_stl(std::span<const std::byte> bytes) {
    std::istringstream input{
        std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()}};
    std::vector<Triangle> result;
    std::string token;
    Triangle triangle;
    unsigned count = 0;
    while (input >> token) {
        if (token == "vertex") {
            auto& point = triangle.points[count++];
            if (!(input >> point.x >> point.y >> point.z)) {
                throw std::runtime_error("Invalid ASCII STL vertex");
            }
            if (count == 3) {
                result.push_back(triangle);
                count = 0;
            }
        }
    }
    if (count != 0 || result.empty()) {
        throw std::runtime_error("ASCII STL contains incomplete or no triangles");
    }
    return result;
}

} // namespace

std::vector<Triangle> read_stl(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    if (bytes.size() >= 84) {
        Reader reader{bytes};
        static_cast<void>(reader.take(80));
        const auto count = reader.u32();
        if (std::uint64_t{count} * 50 == reader.remaining()) {
            return binary_stl(reader, count);
        }
    }
    return ascii_stl(bytes);
}

} // namespace engine::world::storage
