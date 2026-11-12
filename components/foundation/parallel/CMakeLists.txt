add_library(foundation_parallel INTERFACE)
ac_component(foundation_parallel foundation.parallel)
target_sources(foundation_parallel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/engine/foundation/parallel/parallel.hpp")
target_include_directories(foundation_parallel INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(foundation_parallel INTERFACE Threads::Threads)
add_executable(foundation_parallel_test tests/unit/parallel_test.cpp)
ac_test(foundation_parallel_test foundation_parallel)
target_link_libraries(foundation_parallel_test PRIVATE engine::foundation.parallel)
