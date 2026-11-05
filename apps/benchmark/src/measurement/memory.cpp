#include "session.hpp"
namespace engine::app::benchmark {
void observe_memory(Session& session, std::string_view phase, std::optional<unsigned> frame) {
    const auto host = platform::process::memory();
    const auto gpu = session.renderer->memory_statistics();
    json::object sample{{"phase", phase},
                        {"after_frame", frame ? json::value(*frame) : json::value(nullptr)},
                        {"resident_bytes", host.resident_bytes},
                        {"lifetime_peak_resident_bytes", host.peak_resident_bytes},
                        {"private_committed_bytes", host.private_committed_bytes
                                                        ? json::value(*host.private_committed_bytes)
                                                        : json::value(nullptr)},
                        {"renderer_allocated_bytes", gpu.renderer_allocated_bytes},
                        {"heap_budget_supported", gpu.budget_supported}};
    json::array heaps;
    for (const auto& heap : gpu.heaps) {
        heaps.push_back(json::object{
            {"size_bytes", heap.size_bytes},
            {"device_local", heap.device_local},
            {"budget_bytes",
             gpu.budget_supported ? json::value(heap.budget_bytes) : json::value(nullptr)},
            {"estimated_usage_bytes", gpu.budget_supported ? json::value(heap.estimated_usage_bytes)
                                                           : json::value(nullptr)}});
    }
    sample["heaps"] = std::move(heaps);
    session.results.memory.push_back(std::move(sample));
}
} // namespace engine::app::benchmark
