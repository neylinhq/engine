add_library(world_model STATIC
    src/model.cpp
    src/solid_regions.cpp
    src/packing.cpp
    src/bounds.cpp
    src/bound_queue.cpp
    src/world.cpp
    src/editing/chunks.cpp
    src/editing/commit.cpp
    src/generation.cpp
    src/gpu_mirror.cpp
    src/trace.cpp
    src/entity_pack.cpp
    src/entities.cpp
    src/helpers.hpp
    src/state.hpp
    include/engine/world/model/types.hpp
    include/engine/world/model/world.hpp
)
ac_component(world_model world.model)
target_include_directories(world_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(world_model PUBLIC engine::foundation.geometry)
target_link_libraries(world_model PRIVATE engine::foundation.parallel)
add_executable(world_model_test tests/unit/world_test.cpp)
ac_test(world_model_test world_model)
target_link_libraries(world_model_test PRIVATE engine::world.model engine::foundation.geometry)
add_executable(world_gpu_mirror_test tests/unit/gpu_mirror_test.cpp)
ac_test(world_gpu_mirror_test world_model)
target_link_libraries(world_gpu_mirror_test PRIVATE engine::world.model engine::foundation.geometry)
