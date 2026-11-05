#ifndef ENGINE_PLATFORM_PROCESS_PROCESS_HPP
#define ENGINE_PLATFORM_PROCESS_PROCESS_HPP
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace engine::platform::process {
struct Memory {
    std::uint64_t resident_bytes = 0;
    std::uint64_t peak_resident_bytes = 0;
    std::optional<std::uint64_t> private_committed_bytes;
};
struct Identity {
    std::string system;
    std::string processor;
    unsigned hardware_threads = 0;
    std::uint64_t process_id = 0;
};
struct Command {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
};
[[nodiscard]] Memory memory();
[[nodiscard]] Identity identity();
[[nodiscard]] std::filesystem::path executable();
[[nodiscard]] int run(const Command& command);
[[nodiscard]] std::string sha256(std::span<const std::byte> bytes);
[[nodiscard]] std::string file_sha256(const std::filesystem::path& path);
[[nodiscard]] std::string path_text(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path text_path(std::string_view text);
} // namespace engine::platform::process
#endif
