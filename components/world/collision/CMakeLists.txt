set(collision_sources
    include/engine/world/collision/placement.hpp
    include/engine/world/collision/terrain.hpp
    src/placement.cpp src/terrain.cpp src/query.cpp src/query.hpp)
set(collision_tests tests/unit/terrain_test.cpp)
if(ENGINE_DESKTOP)
    add_library(world_collision STATIC ${collision_sources})
    ac_component(world_collision world.collision)
    target_include_directories(world_collision PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(world_collision
        PUBLIC engine::foundation.geometry engine::motion.character engine::world.model
        PRIVATE physics::collision.geometry)
    add_executable(terrain_collision_test ${collision_tests})
    ac_test(terrain_collision_test world_collision)
    target_link_libraries(terrain_collision_test PRIVATE engine::world.collision
        engine::foundation.geometry engine::motion.character engine::world.editing engine::world.model)
else()
    add_library(world_collision INTERFACE)
    ac_component(world_collision world.collision)
    engine_inactive_sources(world_collision "Terrain collision queries require the Physics SDK"
        ${collision_sources} ${collision_tests})
endif()
