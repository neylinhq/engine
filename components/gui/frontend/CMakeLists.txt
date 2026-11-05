set(frontend_sources
    src/frontend.cpp src/input.cpp src/state.hpp src/style.cpp src/menu.cpp src/files.cpp
    src/settings.cpp src/sky.cpp src/debug.cpp src/world.cpp src/material.cpp src/path.cpp
    src/files/path.hpp
    include/engine/gui/frontend/frontend.hpp)
if(ENGINE_DESKTOP)
    add_library(gui_frontend STATIC ${frontend_sources})
    ac_component(gui_frontend gui.frontend)
    target_compile_definitions(gui_frontend PRIVATE GLFW_INCLUDE_NONE)
    target_include_directories(gui_frontend PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_link_libraries(gui_frontend PUBLIC engine::interaction.input engine::motion.camera
        engine::motion.path engine::world.model engine::world.editing engine::world.storage
        engine::rendering.settings engine::platform.window
        PRIVATE engine::foundation.geometry engine_imgui glfw)
    add_executable(gui_frontend_input_test tests/unit/input_test.cpp)
    ac_test(gui_frontend_input_test gui_frontend)
    target_link_libraries(gui_frontend_input_test PRIVATE engine::gui.frontend engine_imgui)
    add_executable(gui_frontend_world_state_test tests/unit/world_state_test.cpp)
    ac_test(gui_frontend_world_state_test gui_frontend)
    target_link_libraries(gui_frontend_world_state_test PRIVATE
        engine::gui.frontend engine::world.editing engine::world.storage engine_imgui)
else()
    ac_inactive_component(gui.frontend "ENGINE_DESKTOP=OFF" ${frontend_sources}
        tests/unit/input_test.cpp tests/unit/world_state_test.cpp)
endif()
