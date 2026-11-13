#include <engine/motion/character/motor.hpp>

#include "contacts.hpp"
#include <engine/foundation/geometry/vector.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace engine::motion::character {
namespace {

constexpr double kWalkSpeed = 4.5;
constexpr double kRunSpeed = 9;
constexpr double kAcceleration = 35;
constexpr double kBraking = 45;
constexpr double kAirControl = 0.25;
constexpr double kGravity = 20;
constexpr double kJumpHeight = 1.25;
constexpr double kTerminalSpeed = 80;
constexpr double kJumpBufferTime = 0.12;
constexpr double kCoyoteTime = 0.10;
constexpr double kEyeClearance = 0.02;
constexpr Vec3 kEyeOffset{0, kEyeHeight, 0};

Vec3 horizontal(Vec3 value) {
    return {value.x, 0, value.z};
}

Vec3 horizontal_velocity(const State& state, const Intent& intent) {
    const auto wish = horizontal(intent.horizontal);
    const auto direction = length(wish) > kMovementTolerance ? normalized(wish) : Vec3{};
    const auto speed = intent.run ? kRunSpeed : kWalkSpeed;
    const auto previous = horizontal(state.velocity);
    if (state.grounded) {
        const auto target = direction * speed;
        const auto difference = target - previous;
        const auto acceleration = direction == Vec3{} ? kBraking : kAcceleration;
        const auto distance = length(difference);
        const auto change = acceleration * kTick;
        return distance <= change ? target : previous + difference * (change / distance);
    }
    const auto remaining = std::max(0.0, speed - dot(previous, direction));
    return previous + direction * std::min(remaining, kAcceleration * kAirControl * kTick);
}

} // namespace

Motor::Motor(Vec3 feet)
    : state_{.feet = feet, .velocity = {}, .eye = feet + kEyeOffset}, previous_eye_(state_.eye) {}

const State& Motor::state() const {
    return state_;
}

Capsule Motor::capsule() const {
    return body_at(state_.feet);
}

bool Motor::recover(const Collision& collision) {
    const auto position = recover_position(collision, state_.feet);
    if (!position) {
        return false;
    }
    if (*position != state_.feet) {
        relocate(*position);
    }
    return true;
}

void Motor::relocate(Vec3 feet) {
    state_ = {.feet = feet, .velocity = {}, .eye = feet + kEyeOffset};
    previous_eye_ = state_.eye;
    previous_step_offset_ = step_offset_ = 0;
    coyote_time_ = 0;
}

void Motor::walk(const Collision& collision, Vec3 velocity) {
    if (velocity == Vec3{}) {
        state_.velocity = {};
        return;
    }
    const auto before = state_.feet;
    const auto flat = slide(collision, before, velocity, 0);
    const auto step =
        flat.blocked ? step_up(collision, before, velocity * kTick, flat.feet) : std::nullopt;
    state_.feet = step.value_or(flat.feet);
    state_.velocity = step ? velocity : flat.velocity;
    const auto floor = step ? step : find_floor(collision, state_.feet, kStepHeight + kGroundProbe);
    state_.grounded = floor.has_value();
    if (floor) {
        state_.feet = *floor;
        step_offset_ += state_.feet.y - before.y;
    }
    state_.velocity.y = 0;
}

void Motor::fall(const Collision& collision, Vec3 velocity) {
    const auto vertical = state_.velocity.y;
    const auto next = std::max(-kTerminalSpeed, vertical - kGravity * kTick);
    velocity.y = (vertical + next) * 0.5;
    const auto moved = slide(collision, state_.feet, velocity, std::max(0.0, velocity.y));
    state_.feet = moved.feet;
    state_.velocity = moved.velocity;
    const auto floor = next <= 0 && moved.velocity.y <= kMovementTolerance
                           ? find_floor(collision, state_.feet, kGroundProbe)
                           : std::nullopt;
    state_.grounded = floor.has_value();
    if (floor) {
        state_.feet = *floor;
        state_.velocity.y = 0;
    } else if (moved.velocity.y == velocity.y) {
        state_.velocity.y = next;
    }
}

void Motor::refresh_floor(const Collision& collision) {
    if (state_.velocity.y != 0) {
        return;
    }
    const auto floor = find_floor(collision, state_.feet, kGroundProbe);
    state_.grounded = floor.has_value();
    if (floor) {
        step_offset_ += floor->y - state_.feet.y;
        state_.feet = *floor;
    }
}

void Motor::tick(const Collision& collision, const Intent& intent) {
    previous_eye_ = state_.feet + kEyeOffset;
    previous_step_offset_ = step_offset_;
    refresh_floor(collision);
    const auto velocity = horizontal_velocity(state_, intent);
    coyote_time_ = state_.grounded ? kCoyoteTime : std::max(0.0, coyote_time_ - kTick);
    if (jump_buffer_ > 0 && coyote_time_ > 0) {
        state_.velocity.y = std::sqrt(2 * kGravity * kJumpHeight);
        jump_buffer_ = coyote_time_ = 0;
        state_.grounded = false;
    }
    jump_buffer_ = std::max(0.0, jump_buffer_ - kTick);
    if (state_.grounded) {
        walk(collision, velocity);
    } else {
        fall(collision, velocity);
    }
    step_offset_ = std::clamp(step_offset_ * std::exp(-18 * kTick), -kStepHeight, kStepHeight);
    if (std::abs(step_offset_) <= kMovementTolerance) {
        step_offset_ = 0;
    }
}

void Motor::place_eye(const Collision& collision) {
    const auto alpha = remaining_time_ / kTick;
    const auto eye = state_.feet + kEyeOffset;
    const auto offset = std::lerp(previous_step_offset_, step_offset_, alpha);
    const auto target = previous_eye_ + (eye - previous_eye_) * alpha - Vec3{0, offset, 0};
    const auto displacement = target - eye;
    if (dot(displacement, displacement) <= kMovementTolerance * kMovementTolerance) {
        state_.eye = target;
        return;
    }
    const Capsule camera{eye - Vec3{0, kEyeClearance, 0}, kEyeClearance, 2 * kEyeClearance};
    const auto hit = collision.sweep(camera, displacement);
    state_.eye = eye + displacement * (hit ? hit->fraction : 1);
}

std::expected<void, std::string> Motor::advance(const Collision& collision, const Intent& intent,
                                                double seconds) {
    if (!finite(state_.feet) || !finite(intent.horizontal) || !std::isfinite(seconds) ||
        seconds < 0) {
        return std::unexpected("Invalid character update");
    }
    auto next = *this;
    if (!next.recover(collision)) {
        return std::unexpected("No free position for the character");
    }
    if (intent.jump_pressed) {
        next.jump_buffer_ = kJumpBufferTime;
    }
    next.remaining_time_ += std::min(seconds, 0.1);
    while (next.remaining_time_ + 1e-12 >= kTick) {
        const auto prior_feet = next.state_.feet;
        next.tick(collision, intent);
        if (next.state_.feet != prior_feet && collision.penetration(body_at(next.state_.feet))) {
            next.relocate(prior_feet);
            next.refresh_floor(collision);
        }
        next.remaining_time_ = std::max(0.0, next.remaining_time_ - kTick);
    }
    next.place_eye(collision);
    *this = next;
    return {};
}

} // namespace engine::motion::character
