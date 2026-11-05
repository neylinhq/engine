#include <algorithm>
#include <cmath>
#include <engine/platform/window_state/window_state.hpp>
#include <limits>
namespace engine::platform::window_state {
namespace {
std::int64_t overlap_area(Rectangle left, Rectangle right) {
    const auto x = std::max<std::int64_t>(
        0, std::min(std::int64_t{left.x} + left.width, std::int64_t{right.x} + right.width) -
               std::max(left.x, right.x));
    const auto y = std::max<std::int64_t>(
        0, std::min(std::int64_t{left.y} + left.height, std::int64_t{right.y} + right.height) -
               std::max(left.y, right.y));
    return x * y;
}
double center_distance(Rectangle window, Rectangle monitor) {
    const auto x = window.x + window.width * 0.5;
    const auto y = window.y + window.height * 0.5;
    const auto dx = x - std::clamp(x, static_cast<double>(monitor.x),
                                   static_cast<double>(monitor.x) + monitor.width);
    const auto dy = y - std::clamp(y, static_cast<double>(monitor.y),
                                   static_cast<double>(monitor.y) + monitor.height);
    return dx * dx + dy * dy;
}
bool contains_center(Rectangle window, Rectangle monitor) {
    const auto x = std::int64_t{window.x} * 2 + window.width;
    const auto y = std::int64_t{window.y} * 2 + window.height;
    return x >= std::int64_t{monitor.x} * 2 && x < (std::int64_t{monitor.x} + monitor.width) * 2 &&
           y >= std::int64_t{monitor.y} * 2 && y < (std::int64_t{monitor.y} + monitor.height) * 2;
}
} // namespace
std::expected<std::size_t, std::string> choose_monitor(Rectangle window,
                                                       std::span<const Monitor> monitors) {
    if (window.width <= 0 || window.height <= 0 || monitors.empty()) {
        return std::unexpected("No valid window or monitors");
    }
    std::size_t best = 0;
    std::int64_t best_area = -1;
    bool best_center = false;
    double best_distance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < monitors.size(); ++index) {
        const auto& monitor = monitors[index];
        if (monitor.full.width <= 0 || monitor.full.height <= 0 || monitor.work.width <= 0 ||
            monitor.work.height <= 0 || !std::isfinite(monitor.desktop_scale_x) ||
            !std::isfinite(monitor.desktop_scale_y) || monitor.desktop_scale_x <= 0 ||
            monitor.desktop_scale_y <= 0) {
            return std::unexpected("Invalid monitor descriptor");
        }
        const auto area = overlap_area(window, monitor.full);
        const auto center = contains_center(window, monitor.full);
        const auto distance = center_distance(window, monitor.full);
        const auto better_tie = area == best_area && ((area == 0 && distance < best_distance) ||
                                                      (area > 0 && center && !best_center));
        if (area > best_area || better_tie) {
            best = index;
            best_area = area;
            best_center = center;
            best_distance = distance;
        }
    }
    return best;
}
} // namespace engine::platform::window_state
