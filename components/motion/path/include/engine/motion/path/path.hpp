#ifndef ENGINE_MOTION_PATH_PATH_HPP
#define ENGINE_MOTION_PATH_PATH_HPP
#include <engine/foundation/geometry/vector.hpp>
#include <expected>
#include <span>
#include <string>
#include <vector>
namespace engine::motion::path {
struct Node {
    foundation::geometry::Vec3 position;
    foundation::geometry::Vec3 direction{0, 0, 1};
    double duration = 1;
    std::string name;
};
struct Sample {
    foundation::geometry::Vec3 position;
    foundation::geometry::Vec3 direction{0, 0, 1};
    double speed = 1;
    bool final_frame = false;
};
class Path {
  public:
    [[nodiscard]] std::expected<void, std::string> replace(std::vector<Node> nodes);
    [[nodiscard]] std::expected<void, std::string> insert(std::size_t index, Node node);
    [[nodiscard]] std::expected<void, std::string> set(std::size_t index, Node node);
    void erase(std::size_t index);
    void start();
    void stop();
    [[nodiscard]] bool playing() const;
    [[nodiscard]] std::span<const Node> nodes() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] double average_milliseconds() const;
    [[nodiscard]] Sample advance(double seconds, bool linear);

  private:
    [[nodiscard]] Sample sample(bool linear) const;
    std::vector<Node> nodes_;
    double time_ = 0;
    bool playing_ = false;
    double elapsed_ = 0;
    std::size_t frames_ = 0;
};
} // namespace engine::motion::path
#endif
