add_library(platform_filesystem STATIC src/filesystem.cpp
    include/engine/platform/filesystem/filesystem.hpp)
ac_component(platform_filesystem platform.filesystem)
target_include_directories(platform_filesystem PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
