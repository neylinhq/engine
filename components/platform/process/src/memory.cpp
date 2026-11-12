#include <engine/platform/process/process.hpp>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#else
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>
#endif
namespace engine::platform::process {
Memory memory() {
    Memory result;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        throw std::runtime_error("Cannot read process memory counters");
    }
    result.resident_bytes = counters.WorkingSetSize;
    result.peak_resident_bytes = counters.PeakWorkingSetSize;
    result.private_committed_bytes = counters.PrivateUsage;
#else
    std::ifstream statm("/proc/self/statm");
    std::uint64_t virtual_pages = 0;
    std::uint64_t resident_pages = 0;
    const auto page_size = sysconf(_SC_PAGESIZE);
    rusage usage{};
    if (!(statm >> virtual_pages >> resident_pages) || page_size <= 0 ||
        getrusage(RUSAGE_SELF, &usage) != 0) {
        throw std::runtime_error("Cannot read process memory counters");
    }
    result.resident_bytes = resident_pages * static_cast<std::uint64_t>(page_size);
    result.peak_resident_bytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
    return result;
}
} // namespace engine::platform::process
