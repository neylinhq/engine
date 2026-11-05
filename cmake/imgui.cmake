set(ENGINE_IMGUI_SOURCE_DIR "" CACHE PATH "Unmodified Dear ImGui 1.91.9b source tree")
if(NOT EXISTS "${ENGINE_IMGUI_SOURCE_DIR}/imgui.h")
    message(FATAL_ERROR "Set ENGINE_IMGUI_SOURCE_DIR to an unmodified Dear ImGui 1.91.9b checkout")
endif()
file(STRINGS "${ENGINE_IMGUI_SOURCE_DIR}/imgui.h" imgui_version REGEX "^#define IMGUI_VERSION ")
if(NOT imgui_version MATCHES "\"1[.]91[.]9b\"")
    message(FATAL_ERROR "NAADF pins the Dear ImGui backend API to version 1.91.9b")
endif()
add_library(engine_imgui STATIC
    "${ENGINE_IMGUI_SOURCE_DIR}/imgui.cpp"
    "${ENGINE_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${ENGINE_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${ENGINE_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${ENGINE_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${ENGINE_IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(engine_imgui SYSTEM PUBLIC "${ENGINE_IMGUI_SOURCE_DIR}")
target_compile_definitions(engine_imgui PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(engine_imgui PRIVATE glfw Vulkan::Vulkan)
ac_external(engine_imgui external.imgui "1.91.9b" "${ENGINE_IMGUI_SOURCE_DIR}")
