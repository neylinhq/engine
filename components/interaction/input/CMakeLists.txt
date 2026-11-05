add_library(interaction_input STATIC src/commands.cpp src/router.cpp
    include/engine/interaction/input/input.hpp)
ac_component(interaction_input interaction.input)
target_include_directories(interaction_input PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
add_executable(input_test tests/unit/input_test.cpp)
ac_test(input_test interaction_input)
target_link_libraries(input_test PRIVATE engine::interaction.input)
