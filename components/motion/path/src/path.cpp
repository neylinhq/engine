#include <algorithm>
#include <cmath>
#include <engine/motion/path/path.hpp>
#include <utility>
namespace engine::motion::path {
namespace {
bool valid(const Node& node) {
    return foundation::geometry::finite(node.position) &&
           foundation::geometry::finite(node.direction) &&
           foundation::geometry::length(node.direction) > 1e-12 && std::isfinite(node.duration) &&
           node.duration > 0;
}
double fade(double value) {
    const auto t = std::clamp(value, 0.0, 1.0);
    return t * t * t * (t * (t * 6 - 15) + 10);
}
void accumulate(Sample& result, const Node& node, double weight) {
    result.position = result.position + node.position * weight;
    result.direction = result.direction + node.direction * weight;
    result.speed += weight / node.duration;
}
} // namespace
std::expected<void, std::string> Path::replace(std::vector<Node> nodes) {
    if (std::ranges::any_of(nodes, [](const auto& node) { return !valid(node); })) {
        return std::unexpected("Path nodes need finite positions, directions and positive times");
    }
    nodes_ = std::move(nodes);
    stop();
    return {};
}
std::expected<void, std::string> Path::insert(std::size_t index, Node node) {
    if (index > nodes_.size() || !valid(node)) {
        return std::unexpected("Invalid path insertion");
    }
    nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(index), std::move(node));
    stop();
    return {};
}
std::expected<void, std::string> Path::set(std::size_t index, Node node) {
    if (index >= nodes_.size() || !valid(node)) {
        return std::unexpected("Invalid path node");
    }
    nodes_[index] = std::move(node);
    return {};
}
void Path::erase(std::size_t index) {
    if (index < nodes_.size()) {
        nodes_.erase(nodes_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    stop();
}
void Path::start() {
    time_ = 0;
    playing_ = !nodes_.empty();
    elapsed_ = 0;
    frames_ = 0;
}
void Path::stop() {
    playing_ = false;
    time_ = 0;
}
bool Path::playing() const {
    return playing_;
}
std::span<const Node> Path::nodes() const {
    return nodes_;
}
double Path::duration() const {
    double result = 0;
    for (const auto& node : nodes_) {
        result += node.duration;
    }
    return result;
}
double Path::average_milliseconds() const {
    return frames_ == 0 ? 0 : elapsed_ * 1000 / static_cast<double>(frames_);
}
Sample Path::sample(bool linear) const {
    Sample result{{}, {}, 0, false};
    if (nodes_.empty()) {
        return result;
    }
    const auto base = static_cast<std::size_t>(time_);
    const auto begin = linear || base == 0 ? base : base - 1;
    const auto end = std::min(nodes_.size(), base + (linear ? 2 : 3));
    double total = 0;
    for (auto index = begin; index < end; ++index) {
        const auto distance = std::abs(static_cast<double>(index) - time_);
        const auto weight = linear ? 1.0 - distance : fade(1.0 - distance * 0.5);
        accumulate(result, nodes_[index], weight);
        total += weight;
    }
    result.position = result.position / total;
    result.speed /= total;
    result.direction = foundation::geometry::normalized(result.direction);
    if (foundation::geometry::length(result.direction) < 1e-12) {
        result.direction = foundation::geometry::normalized(nodes_[base].direction);
    }
    return result;
}
Sample Path::advance(double seconds, bool linear) {
    auto result = sample(linear);
    if (!playing_ || !std::isfinite(seconds) || seconds < 0) {
        return result;
    }
    elapsed_ += seconds;
    ++frames_;
    const auto last = static_cast<double>(nodes_.size() - 1);
    if (time_ >= last) {
        playing_ = false;
        result.final_frame = true;
    }
    time_ = std::min(last, time_ + result.speed * seconds);
    return result;
}
} // namespace engine::motion::path
