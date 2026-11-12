find_package(ImGuiSources CONFIG REQUIRED)
set(imgui_source "${ImGuiSources_INCLUDE_DIRS}")
add_library(engine_imgui STATIC
    "${imgui_source}/imgui.cpp"
    "${imgui_source}/imgui_draw.cpp"
    "${imgui_source}/imgui_tables.cpp"
    "${imgui_source}/imgui_widgets.cpp"
    "${imgui_source}/backends/imgui_impl_glfw.cpp"
    "${imgui_source}/backends/imgui_impl_vulkan.cpp")
target_include_directories(engine_imgui SYSTEM PUBLIC "${imgui_source}")
target_compile_definitions(engine_imgui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(engine_imgui PRIVATE glfw Vulkan::Vulkan)
ac_external(engine_imgui external.imgui "${ImGuiSources_VERSION}" "${imgui_source}")
