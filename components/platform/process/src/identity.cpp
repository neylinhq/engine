#include <engine/platform/process/process.hpp>
#include <fstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif
namespace engine::platform::process {
std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::filesystem::path text_path(std::string_view text) {
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
std::filesystem::path executable() {
#ifdef _WIN32
    std::wstring value(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    if (size == 0 || size >= value.size()) {
        throw std::runtime_error("Cannot resolve executable path");
    }
    value.resize(size);
    return value;
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}
Identity identity() {
    Identity result;
    result.hardware_threads = std::thread::hardware_concurrency();
#ifdef _WIN32
    result.system = "Windows";
    result.process_id = GetCurrentProcessId();
    std::wstring cpu(1024, L'\0');
    const auto size = GetEnvironmentVariableW(L"PROCESSOR_IDENTIFIER", cpu.data(),
                                              static_cast<DWORD>(cpu.size()));
    if (size > 0 && size < cpu.size()) {
        cpu.resize(size);
        result.processor = path_text(cpu);
    }
#else
    utsname system{};
    if (uname(&system) == 0) {
        result.system = std::string(system.sysname) + " " + system.release + " " + system.machine;
    }
    result.process_id = static_cast<std::uint64_t>(getpid());
    std::ifstream cpu("/proc/cpuinfo");
    for (std::string line; std::getline(cpu, line);) {
        if (line.starts_with("model name")) {
            result.processor = line.substr(line.find(':') + 2);
            break;
        }
    }
#endif
    return result;
}
} // namespace engine::platform::process
