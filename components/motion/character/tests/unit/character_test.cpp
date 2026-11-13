#include <algorithm>
#include <cmath>
#include <engine/foundation/geometry/vector.hpp>
#include <engine/motion/character/motor.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace engine::motion::character {
namespace {

class Floor final : public Collision {
  public:
    explicit Floor(bool enabled = true) : enabled_(enabled) {}

    [[nodiscard]] std::optional<SweepHit> sweep(Capsule capsule, Vec3 delta,
                                                SweepFilter filter) const override {
        if (!enabled_ || delta.y >= 0 || capsule.feet.y > -delta.y) {
            return std::nullopt;
        }
        const auto fraction = -capsule.feet.y / delta.y;
        const auto point = capsule.feet + delta * fraction;
        const SweepHit hit{fraction, {0, 1, 0}, {point.x, 0, point.z}};
        return !filter.accept || filter.accept(hit, filter.context) ? std::optional{hit}
                                                                    : std::nullopt;
    }

    [[nodiscard]] std::optional<Penetration> penetration(Capsule) const override {
        return std::nullopt;
    }

  private:
    bool enabled_;
};

constexpr double kStartHeight = 0.02;
constexpr Intent kForward{.horizontal = {0, 0, 1}};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void advance(Motor& motor, const Collision& field, const Intent& intent, int ticks = 1) {
    for (int tick = 0; tick < ticks; ++tick) {
        motor.advance(field, intent, kTick).value();
    }
}

void locomotion() {
    const Floor floor;
    Motor motor({0, kStartHeight, 0});
    advance(motor, floor, {});
    require(motor.state().grounded, "Character did not settle onto floor");
    advance(motor, floor, kForward, 90);
    require(std::abs(motor.state().velocity.z - 4.5) < 1e-8, "Walk speed differs from 4.5 m/s");
    const Intent run{.horizontal = {1, 0, 1}, .run = true};
    advance(motor, floor, run, 90);
    require(std::abs(length(motor.state().velocity) - 9) < 1e-8,
            "Diagonal running exceeded or missed 9 m/s");
    advance(motor, floor, {}, 60);
    require(length(motor.state().velocity) < 1e-8 && motor.state().grounded,
            "Character failed to brake on the ground");
}

void jump() {
    const Floor floor;
    Motor motor({0, kStartHeight, 0});
    advance(motor, floor, {});
    advance(motor, floor, {.jump_pressed = true});
    auto peak = motor.state().feet.y;
    for (int tick = 0; tick < 160; ++tick) {
        advance(motor, floor, {});
        peak = std::max(peak, motor.state().feet.y);
    }
    require(std::abs(peak - kStartHeight - 1.25) < 0.002, "Jump apex differs from 1.25 m");
    require(motor.state().grounded && std::abs(motor.state().feet.y - kStartHeight) < 1e-9,
            "Jump did not land");
    require(motor.state().feet.z == 0, "Jump from rest injected horizontal movement");
}

void frame_partition() {
    const Floor floor;
    Motor fast({0, kStartHeight, 0});
    Motor slow = fast;
    advance(fast, floor, kForward, 96);
    for (int frame = 0; frame < 24; ++frame) {
        slow.advance(floor, kForward, 4 * kTick).value();
    }
    require(length(fast.state().feet - slow.state().feet) < 1e-9 &&
                length(fast.state().velocity - slow.state().velocity) < 1e-9,
            "Motor depends on frame partition");
    const auto prior = fast.state().feet;
    require(!fast.advance(floor, kForward, std::numeric_limits<double>::quiet_NaN()) &&
                fast.state().feet == prior,
            "Rejected update changed the motor state");
}

void coyote_and_buffer() {
    const Floor floor;
    const Floor air{false};
    Motor leaving({0, kStartHeight, 0});
    advance(leaving, floor, {});
    advance(leaving, air, {}, 4);
    require(!leaving.state().grounded, "Leaving support did not begin falling");
    advance(leaving, air, {.jump_pressed = true});
    require(leaving.state().velocity.y > 6, "Jump during the coyote window was lost");
    Motor landing({0, 0.04, 0});
    advance(landing, floor, {.jump_pressed = true});
    advance(landing, floor, {}, 8);
    require(landing.state().velocity.y > 6, "Buffered jump was lost at landing");
}

void verify() {
    locomotion();
    jump();
    frame_partition();
    coyote_and_buffer();
}

} // namespace
} // namespace engine::motion::character

int main() {
    try {
        engine::motion::character::verify();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
