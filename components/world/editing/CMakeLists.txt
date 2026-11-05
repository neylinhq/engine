add_library(world_editing STATIC
    src/editing.cpp
    src/brush.cpp
    include/engine/world/editing/editing.hpp
)
ac_component(world_editing world.editing)
target_include_directories(world_editing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(world_editing PUBLIC engine::world.model engine::foundation.geometry)
target_link_libraries(world_editing PRIVATE engine::foundation.parallel)
add_executable(world_editing_test tests/unit/editing_test.cpp)
ac_test(world_editing_test world_editing)
target_link_libraries(world_editing_test PRIVATE engine::world.editing engine::world.model
    engine::foundation.geometry)
