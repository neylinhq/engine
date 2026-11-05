add_library(platform_process STATIC
    include/engine/platform/process/process.hpp
    src/identity.cpp src/memory.cpp src/child.cpp src/fingerprint.cpp)
ac_component(platform_process platform.process)
target_include_directories(platform_process PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(platform_process PRIVATE OpenSSL::Crypto)
add_executable(platform_process_test tests/unit/process_test.cpp)
ac_test(platform_process_test platform_process)
target_link_libraries(platform_process_test PRIVATE engine::platform.process)
