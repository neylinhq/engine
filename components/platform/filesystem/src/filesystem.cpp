#include <engine/platform/filesystem/filesystem.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace engine::platform::filesystem {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path reserve_temporary(const std::filesystem::path& parent) {
    static std::atomic<std::uint64_t> sequence = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto index = sequence.fetch_add(1, std::memory_order_relaxed);
        const auto directory =
            parent / (".engine-" + std::to_string(stamp) + "-" + std::to_string(index));
        if (std::filesystem::create_directory(directory)) {
            return directory;
        }
    }
    throw std::runtime_error("Cannot reserve a temporary directory in " + path_text(parent));
}

} // namespace

std::vector<std::byte> read(const std::filesystem::path& path, std::size_t max_bytes) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Expected a regular file: " + path_text(path));
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    if (size < 0) {
        throw std::runtime_error("Cannot open file: " + path_text(path));
    }
    if (static_cast<std::uint64_t>(size) > max_bytes) {
        throw std::runtime_error("File exceeds " + std::to_string(max_bytes) +
                                 " bytes: " + path_text(path));
    }
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(result.data()), size)) {
        throw std::runtime_error("Cannot read file completely: " + path_text(path));
    }
    return result;
}

void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    const auto destination = std::filesystem::absolute(path);
    const auto directory = reserve_temporary(destination.parent_path());
    const auto temporary = directory / "data";
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) {
            throw std::runtime_error("Cannot write file: " + path_text(path));
        }
        std::filesystem::rename(temporary, destination);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(directory, ignored);
        throw;
    }
    std::error_code ignored;
    std::filesystem::remove(directory, ignored);
}

} // namespace engine::platform::filesystem
