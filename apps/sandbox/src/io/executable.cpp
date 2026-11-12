#include "executable.hpp"
#include "path.hpp"
#include <cstdlib>
#include <memory>
#include <string>
namespace engine::app::sandbox {
namespace {
std::string process_path() {
#ifdef _MSC_VER
    wchar_t* value = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&value, &size, L"PATH") != 0 || !value) {
        return {};
    }
    const std::unique_ptr<wchar_t, decltype(&std::free)> owned(value, &std::free);
    return path_to_utf8(std::filesystem::path(owned.get()));
#elif defined(_WIN32)
    const auto* value = _wgetenv(L"PATH");
    return value ? path_to_utf8(std::filesystem::path(value)) : std::string{};
#else
    const auto* value = std::getenv("PATH");
    return value ? value : std::string{};
#endif
}
std::filesystem::path resolved(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path);
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute : canonical;
}
bool executable(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        return false;
    }
#ifndef _WIN32
    using std::filesystem::perms;
    constexpr auto kExecute = perms::owner_exec | perms::group_exec | perms::others_exec;
    return (status.permissions() & kExecute) != perms::none;
#else
    return true;
#endif
}
} // namespace
std::filesystem::path find_executable(std::filesystem::path invocation,
                                      std::string_view search_path) {
    if (invocation.has_parent_path()) {
        return resolved(invocation);
    }
#ifdef _WIN32
    constexpr char kSeparator = ';';
    if (!invocation.has_extension()) {
        invocation += L".exe";
    }
#else
    constexpr char kSeparator = ':';
#endif
    for (std::size_t begin = 0; begin <= search_path.size();) {
        const auto end = search_path.find(kSeparator, begin);
        auto directory = search_path.substr(begin, end - begin);
#ifdef _WIN32
        if (directory.size() > 1 && directory.front() == '"' && directory.back() == '"') {
            directory = directory.substr(1, directory.size() - 2);
        }
#endif
        const auto candidate = path_from_utf8(directory) / invocation;
        if (executable(candidate)) {
            return resolved(candidate);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return resolved(invocation);
}
std::filesystem::path executable_directory(std::string_view invocation) {
    return find_executable(path_from_utf8(invocation), process_path()).parent_path();
}
} // namespace engine::app::sandbox
