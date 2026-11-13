set(scenario_sources src/scenario/scenario.hpp src/scenario/json.hpp src/scenario/json.cpp
    src/scenario/parse.cpp src/scenario/events.cpp src/scenario/camera.cpp)
set(benchmark_sources src/main.cpp src/command.hpp src/command.cpp src/repeat.cpp
    ${scenario_sources}
    src/measurement/session.hpp src/measurement/session.cpp src/measurement/frames.cpp
    src/measurement/actions.cpp src/measurement/identity.cpp src/measurement/memory.cpp
    src/report/report.hpp src/report/statistics.cpp src/report/summary.cpp
    src/report/samples.cpp src/report/comparison.cpp)
set(benchmark_dependencies engine::foundation.geometry engine::motion.camera
    engine::platform.process engine::platform.filesystem engine::platform.window
    engine::world.model engine::world.editing
    engine::world.storage engine::rendering.settings engine::rendering.configuration
    engine::rendering.vulkan Boost::json)
if(ENGINE_DESKTOP)
    add_executable(benchmark ${benchmark_sources})
    ac_application(benchmark app.benchmark)
    target_link_libraries(benchmark PRIVATE ${benchmark_dependencies})
    target_compile_definitions(benchmark PRIVATE
        ENGINE_APP_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        ENGINE_APP_BENCHMARK_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        ENGINE_APP_BENCHMARK_VERSION="${PROJECT_VERSION}")
    if(MINGW)
        target_link_options(benchmark PRIVATE -municode)
    endif()
    add_dependencies(benchmark engine_runtime_shaders)
    install(TARGETS benchmark RUNTIME DESTINATION bin)
    install(DIRECTORY resources/scenarios/ DESTINATION benchmark/scenarios)
    install(DIRECTORY schemas/ DESTINATION benchmark/schemas)
    install(DIRECTORY docs/ DESTINATION benchmark/docs)
    add_executable(benchmark_contract_test tests/unit/contract_test.cpp ${scenario_sources} src/report/statistics.cpp)
    ac_test(benchmark_contract_test benchmark)
    target_link_libraries(benchmark_contract_test PRIVATE ${benchmark_dependencies})
    add_executable(benchmark_scenario_test tests/integration/scenario_test.cpp)
    ac_register(benchmark_scenario_test app.benchmark TRUE)
    target_link_libraries(benchmark_scenario_test PRIVATE engine::platform.process engine::world.model engine::world.storage Boost::json)
    if(ENGINE_GPU_TESTS)
        add_test(NAME benchmark_scenario_test COMMAND benchmark_scenario_test "$<TARGET_FILE:benchmark>"
            "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_CURRENT_BINARY_DIR}/integration")
    endif()
else()
    add_library(benchmark INTERFACE)
    ac_application(benchmark app.benchmark)
    engine_inactive_sources(benchmark "GPU benchmark requires the desktop build"
        ${benchmark_sources} tests/unit/contract_test.cpp tests/integration/scenario_test.cpp)
endif()
engine_assets(benchmark benchmark resources/scenarios)
engine_assets(benchmark benchmark schemas)
