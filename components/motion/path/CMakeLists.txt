add_library(motion_path STATIC src/path.cpp include/engine/motion/path/path.hpp)
ac_component(motion_path motion.path)
target_include_directories(motion_path PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(motion_path PUBLIC engine::foundation.geometry)
add_executable(path_test tests/unit/path_test.cpp)
ac_test(path_test motion_path)
target_link_libraries(path_test PRIVATE engine::motion.path)
