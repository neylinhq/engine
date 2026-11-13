add_library(rendering_configuration STATIC
    include/engine/rendering/configuration/configuration.hpp
    src/settings.hpp src/configuration.cpp src/render_settings.cpp src/atmosphere_settings.cpp
    src/file.cpp)
ac_component(rendering_configuration rendering.configuration)
target_include_directories(rendering_configuration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rendering_configuration PUBLIC engine::rendering.settings
    PRIVATE engine::platform.filesystem Boost::json)
add_executable(rendering_configuration_test tests/unit/configuration_test.cpp)
ac_test(rendering_configuration_test rendering_configuration)
target_link_libraries(rendering_configuration_test PRIVATE engine::rendering.configuration engine::rendering.settings)
