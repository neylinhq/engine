add_library(motion_camera STATIC src/camera.cpp src/projection.cpp
    include/engine/motion/camera/camera.hpp)
ac_component(motion_camera motion.camera)
target_include_directories(motion_camera PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(motion_camera PUBLIC engine::foundation.geometry)
add_executable(camera_test tests/unit/camera_test.cpp)
ac_test(camera_test motion_camera)
target_link_libraries(camera_test PRIVATE engine::motion.camera)
