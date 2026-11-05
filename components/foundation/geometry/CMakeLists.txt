add_library(foundation_geometry INTERFACE)
ac_component(foundation_geometry foundation.geometry)
target_sources(foundation_geometry PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/engine/foundation/geometry/vector.hpp")
target_include_directories(foundation_geometry INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
add_executable(geometry_test tests/unit/vector_test.cpp)
ac_test(geometry_test foundation_geometry)
target_link_libraries(geometry_test PRIVATE engine::foundation.geometry)
