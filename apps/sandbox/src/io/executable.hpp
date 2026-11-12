#ifndef ENGINE_APP_SANDBOX_SRC_IO_EXECUTABLE_HPP
#define ENGINE_APP_SANDBOX_SRC_IO_EXECUTABLE_HPP
#include <filesystem>
#include <string_view>
namespace engine::app::sandbox {
[[nodiscard]] std::filesystem::path find_executable(std::filesystem::path invocation,
                                                    std::string_view search_path);
[[nodiscard]] std::filesystem::path executable_directory(std::string_view invocation);
} // namespace engine::app::sandbox
#endif
