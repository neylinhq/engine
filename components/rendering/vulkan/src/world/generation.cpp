#include "generation.hpp"

#include "../generation/state.hpp"

namespace engine::rendering::vulkan {

GenerationResult generate_world(WorldCompute& compute, const world::model::Model& source,
                                world::model::IVec3 requested, int segment_groups) {
    if (uniform_source(source, requested, segment_groups)) {
        return import_uniform(compute, source);
    }
    auto state = generation_state(compute, source, requested, segment_groups);
    generation_allocate(state);
    generation_segments(state);
    auto result = generation_readback(state);
    generation_bounds(state);
    return result;
}

} // namespace engine::rendering::vulkan
