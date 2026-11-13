#ifndef ENGINE_WORLD_EDITING_EDITING_HPP
#define ENGINE_WORLD_EDITING_EDITING_HPP

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

// Returns false if the placement guard rejects the edit.
bool apply_brush(model::World& world, const Brush& brush, model::PlacementGuard guard = {});
void flood_fill(model::World& world, model::Vec3i start, std::uint16_t material);
bool stamp_model(model::World& world, const model::Model& source, model::Vec3i origin,
                 model::PlacementGuard guard = {});

} // namespace engine::world::editing

#endif
