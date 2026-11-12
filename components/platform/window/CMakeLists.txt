set(window_sources src/window.cpp src/window_impl.hpp src/input.cpp src/placement.cpp
    include/engine/platform/window/window.hpp)
if(ENGINE_DESKTOP)
    add_library(platform_window STATIC ${window_sources})
    ac_component(platform_window platform.window)
    target_compile_definitions(platform_window PRIVATE GLFW_INCLUDE_VULKAN GLFW_INCLUDE_NONE)
    target_include_directories(platform_window PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_link_libraries(platform_window PUBLIC engine::interaction.input engine::platform.window_state
        PRIVATE glfw Vulkan::Vulkan)
else()
    add_library(platform_window INTERFACE)
    ac_component(platform_window platform.window)
    engine_inactive_sources(platform_window "GLFW adapter requires the desktop build" ${window_sources})
endif()
