#include "report.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
namespace engine::app::benchmark {
namespace {
double mean(std::span<const double> values) {
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}
double resampled_mean(std::span<const double> values, std::mt19937& random) {
    std::uniform_int_distribution<std::size_t> index(0, values.size() - 1);
    double sum = 0;
    for (std::size_t count = 0; count < values.size(); ++count) {
        sum += values[index(random)];
    }
    return sum / static_cast<double>(values.size());
}
} // namespace
double quantile(std::span<const double> sorted, double probability) {
    if (sorted.empty() || probability < 0 || probability > 1) {
        throw std::invalid_argument("Invalid quantile input");
    }
    const auto index = static_cast<double>(sorted.size() - 1) * probability;
    const auto low = static_cast<std::size_t>(std::floor(index));
    const auto high = std::min(low + 1, sorted.size() - 1);
    return std::lerp(sorted[low], sorted[high], index - static_cast<double>(low));
}
json::value distribution(std::span<const double> values) {
    if (values.empty()) {
        return nullptr;
    }
    std::vector<double> sorted(values.begin(), values.end());
    for (const auto value : sorted) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Nonfinite measurement");
        }
    }
    std::ranges::sort(sorted);
    const auto average = mean(values);
    double squares = 0;
    for (const auto value : values) {
        squares += (value - average) * (value - average);
    }
    return json::object{
        {"count", values.size()},
        {"mean", average},
        {"stddev", values.size() > 1
                       ? json::value(std::sqrt(squares / static_cast<double>(values.size() - 1)))
                       : json::value(nullptr)},
        {"min", sorted.front()},
        {"p50", quantile(sorted, 0.5)},
        {"p95", quantile(sorted, 0.95)},
        {"p99", quantile(sorted, 0.99)},
        {"max", sorted.back()}};
}
json::object effect(std::span<const double> baseline, std::span<const double> candidate) {
    if (baseline.empty() || candidate.empty() || mean(baseline) <= 0) {
        return {{"ratio", nullptr}, {"percent_change", nullptr}, {"ci95", nullptr}};
    }
    const auto ratio = mean(candidate) / mean(baseline);
    json::object result{{"ratio", ratio},
                        {"percent_change", 100 * (ratio - 1)},
                        {"ci95", nullptr},
                        {"baseline_runs", baseline.size()},
                        {"candidate_runs", candidate.size()}};
    if (baseline.size() < 3 || candidate.size() < 3) {
        result["interval_status"] = "requires_at_least_three_independent_runs_per_build";
        return result;
    }
    std::mt19937 random(0x20261105);
    std::vector<double> ratios;
    ratios.reserve(10000);
    for (unsigned index = 0; index < 10000; ++index) {
        const auto divisor = resampled_mean(baseline, random);
        if (divisor <= 0) {
            result["interval_status"] = "zero_resampled_baseline";
            return result;
        }
        ratios.push_back(resampled_mean(candidate, random) / divisor);
    }
    std::ranges::sort(ratios);
    result["ci95"] = json::array{quantile(ratios, 0.025), quantile(ratios, 0.975)};
    result["interval_status"] = "percentile_bootstrap_of_independent_process_runs";
    result["bootstrap_resamples"] = ratios.size();
    return result;
}
} // namespace engine::app::benchmark
