add_library(platform_window_state STATIC src/monitor.cpp src/window_state.cpp
    include/engine/platform/window_state/window_state.hpp)
ac_component(platform_window_state platform.window_state)
target_include_directories(platform_window_state PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
add_executable(window_state_test tests/unit/window_state_test.cpp)
ac_test(window_state_test platform_window_state)
target_link_libraries(window_state_test PRIVATE engine::platform.window_state)
