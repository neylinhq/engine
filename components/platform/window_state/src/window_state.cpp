#include <engine/platform/window_state/window_state.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace engine::platform::window_state {
namespace {
Placement restore(Placement saved, const Monitor& monitor) {
    const auto same = saved.monitor == monitor.id &&
                      saved.desktop_scale_x == monitor.desktop_scale_x &&
                      saved.desktop_scale_y == monitor.desktop_scale_y;
    if (!same) {
        auto& rectangle = saved.restore_rectangle;
        const auto width = rectangle.width * monitor.desktop_scale_x / saved.desktop_scale_x;
        const auto height = rectangle.height * monitor.desktop_scale_y / saved.desktop_scale_y;
        rectangle.width =
            static_cast<int>(std::clamp(width, 1.0, static_cast<double>(monitor.work.width)));
        rectangle.height =
            static_cast<int>(std::clamp(height, 1.0, static_cast<double>(monitor.work.height)));
        rectangle.x = static_cast<int>(std::clamp<std::int64_t>(
            rectangle.x, monitor.work.x,
            std::int64_t{monitor.work.x} + monitor.work.width - rectangle.width));
        rectangle.y = static_cast<int>(std::clamp<std::int64_t>(
            rectangle.y, monitor.work.y,
            std::int64_t{monitor.work.y} + monitor.work.height - rectangle.height));
    }
    saved.rectangle = saved.restore_rectangle;
    saved.monitor = monitor.id;
    saved.desktop_scale_x = monitor.desktop_scale_x;
    saved.desktop_scale_y = monitor.desktop_scale_y;
    return saved;
}
} // namespace
std::expected<Transition, std::string>
WindowState::toggle(Placement current, std::span<const Monitor> monitors) const {
    const auto selected = choose_monitor(current.rectangle, monitors);
    if (!selected) {
        return std::unexpected(selected.error());
    }
    if (!std::isfinite(current.desktop_scale_x) || !std::isfinite(current.desktop_scale_y) ||
        current.desktop_scale_x <= 0 || current.desktop_scale_y <= 0 ||
        current.restore_rectangle.width <= 0 || current.restore_rectangle.height <= 0) {
        return std::unexpected("Invalid ordinary window snapshot");
    }
    const auto& monitor = monitors[*selected];
    if (ordinary_) {
        const auto found = std::find_if(monitors.begin(), monitors.end(), [&](const auto& value) {
            return value.id == ordinary_->monitor;
        });
        const auto& destination = found == monitors.end() ? monitor : *found;
        return Transition{restore(*ordinary_, destination), *ordinary_, false, revision_};
    }
    auto target = current;
    target.rectangle = monitor.full;
    target.monitor = monitor.id;
    target.maximized = false;
    target.decorated = false;
    target.resizable = false;
    return Transition{target, current, true, revision_};
}
std::expected<void, std::string> WindowState::commit(const Transition& transition) {
    if (transition.revision != revision_ || transition.borderless == ordinary_.has_value()) {
        return std::unexpected("Stale or conflicting window transition");
    }
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected("Window transition revision exhausted");
    }
    ordinary_ = transition.borderless ? std::optional{transition.ordinary} : std::nullopt;
    ++revision_;
    return {};
}
bool WindowState::borderless() const {
    return ordinary_.has_value();
}
std::uint64_t WindowState::revision() const {
    return revision_;
}
} // namespace engine::platform::window_state
