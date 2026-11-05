#ifndef ENGINE_APP_BENCHMARK_REPORT_HPP
#define ENGINE_APP_BENCHMARK_REPORT_HPP
#include "../measurement/session.hpp"
#include <span>
namespace engine::app::benchmark {
[[nodiscard]] double quantile(std::span<const double> sorted, double probability);
[[nodiscard]] json::value distribution(std::span<const double> values);
[[nodiscard]] json::object effect(std::span<const double> baseline,
                                  std::span<const double> candidate);
[[nodiscard]] json::object summary(const Session& session);
[[nodiscard]] json::object write_samples(const Session& session);
[[nodiscard]] json::array read_runs(const std::filesystem::path& directory);
[[nodiscard]] json::object aggregate(const json::array& reports);
} // namespace engine::app::benchmark
#endif
