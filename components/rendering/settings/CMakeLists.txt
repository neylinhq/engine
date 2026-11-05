add_library(rendering_settings INTERFACE)
ac_component(rendering_settings rendering.settings)
target_sources(rendering_settings PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/engine/rendering/settings/settings.hpp")
target_include_directories(rendering_settings INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
