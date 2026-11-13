#ifndef ENGINE_PLATFORM_FILESYSTEM_FILESYSTEM_HPP
#define ENGINE_PLATFORM_FILESYSTEM_FILESYSTEM_HPP

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace engine::platform::filesystem {

[[nodiscard]] std::vector<std::byte> read(const std::filesystem::path& path, std::size_t max_bytes);
// The destination's parent directory must exist.
void write(const std::filesystem::path& path, std::span<const std::byte> bytes);

} // namespace engine::platform::filesystem

#endif
