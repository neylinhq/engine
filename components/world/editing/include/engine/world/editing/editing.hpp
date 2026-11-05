#pragma once

#include <engine/world/model/world.hpp>

namespace engine::world::editing {

inline constexpr double kMaximumBrushRadius = 400;

enum class Tool { none, sphere, cube, paint, flood_fill, model };

struct Brush {
    Tool tool = Tool::sphere;
    model::Vec3 center;
    double radius = 10;
    std::uint16_t material = 1;
    bool erase = false;
};

void apply_brush(model::World& world, const Brush& brush);
void flood_fill(model::World& world, model::IVec3 start, std::uint16_t material);
void stamp_model(model::World& world, const model::Model& source, model::IVec3 origin);

} // namespace engine::world::editing
