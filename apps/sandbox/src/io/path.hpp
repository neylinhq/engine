#ifndef ENGINE_APP_SANDBOX_PATH_HPP
#define ENGINE_APP_SANDBOX_PATH_HPP
#include <filesystem>
#include <string>
#include <string_view>
namespace engine::app::sandbox {
inline std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
} // namespace engine::app::sandbox
#endif
