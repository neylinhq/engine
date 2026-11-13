#ifndef ENGINE_MOTION_CHARACTER_MOTOR_HPP
#define ENGINE_MOTION_CHARACTER_MOTOR_HPP

#include <engine/motion/character/collision.hpp>
#include <expected>
#include <string>

namespace engine::motion::character {

inline constexpr double kTick = 1.0 / 120;
inline constexpr double kRadius = 0.34375;
inline constexpr double kHeight = 1.28125;
inline constexpr double kEyeHeight = 1.03125;
inline constexpr double kStepHeight = 0.35 * kHeight;

struct Intent {
    Vec3 horizontal{};
    bool run = false;
    bool jump_pressed = false;
};

struct State {
    Vec3 feet;
    Vec3 velocity;
    Vec3 eye;
    bool grounded = false;
};

class Motor {
  public:
    explicit Motor(Vec3 feet);
    [[nodiscard]] std::expected<void, std::string> advance(const Collision& collision,
                                                           const Intent& intent, double seconds);
    [[nodiscard]] const State& state() const;
    [[nodiscard]] Capsule capsule() const;

  private:
    void tick(const Collision& collision, const Intent& intent);
    void walk(const Collision& collision, Vec3 velocity);
    void fall(const Collision& collision, Vec3 velocity);
    void refresh_floor(const Collision& collision);
    [[nodiscard]] bool recover(const Collision& collision);
    void relocate(Vec3 feet);
    void place_eye(const Collision& collision);

    State state_;
    Vec3 previous_eye_{};
    double remaining_time_ = 0;
    double jump_buffer_ = 0;
    double coyote_time_ = 0;
    double step_offset_ = 0;
    double previous_step_offset_ = 0;
};

} // namespace engine::motion::character

#endif
