#ifndef ENGINE_GUI_FRONTEND_SRC_FILES_PATH_HPP
#define ENGINE_GUI_FRONTEND_SRC_FILES_PATH_HPP
#include <filesystem>
#include <string>
#include <string_view>
namespace engine::gui::frontend {
inline std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
} // namespace engine::gui::frontend
#endif
