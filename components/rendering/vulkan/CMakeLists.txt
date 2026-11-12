set(renderer_sources
    include/engine/rendering/vulkan/renderer.hpp
    src/frame/atmosphere.cpp
    src/frame/matrices.cpp
    src/frame/output.cpp
    src/frame/parameters.cpp
    src/frame/recording.cpp
    src/frame/resources.cpp
    src/frame/settings.cpp
    src/frame/upload.cpp
    src/frame/values.hpp
    src/gpu/allocation.cpp
    src/gpu/chunks.cpp
    src/gpu/device.cpp
    src/gpu/device.hpp
    src/gpu/interface.cpp
    src/gpu/submission.cpp
    src/gpu/timing.cpp
    src/gpu/diagnostics.cpp
    src/gpu/surface.cpp
    src/image/capture.cpp
    src/image/png.cpp
    src/program/descriptors.cpp
    src/program/pipeline.cpp
    src/program/reflection.cpp
    src/program/reflection.hpp
    src/renderer.cpp
    src/state.hpp)
list(APPEND renderer_sources
    src/world/compute.hpp src/world/compute.cpp src/world/bindings.cpp src/world/transfer.cpp
    src/world/bridge.cpp src/world/generation.hpp src/world/generation.cpp
    src/generation/state.hpp src/generation/setup.cpp src/generation/segments.cpp
    src/generation/readback.cpp src/generation/uniform.cpp
    src/world/updates.hpp src/world_updates/groups.hpp src/world_updates/groups.cpp
    src/world_updates/support.hpp src/world_updates/bounds.cpp src/world_updates/changes.cpp
    src/world_updates/entities.cpp)
set(contract_sources tests/unit/contracts_test.cpp)
set(lifecycle_sources tests/integration/lifecycle_test.cpp)
set(frame_wait_sources tests/integration/frame_wait_test.cpp)
set(ray_test_sources tests/integration/ray_contract_test.cpp
    tests/support/trace_probe.hpp tests/support/trace_device.cpp tests/support/trace_upload.cpp)
set(generation_test_sources tests/integration/generation_contract_test.cpp)
set(performance_test_sources tests/integration/large_world_performance_test.cpp)
set(chunk_performance_test_sources tests/integration/chunk_layout_performance_test.cpp
    tests/support/chunks/performance.hpp tests/support/chunks/performance.cpp
    tests/support/chunks/pattern.hpp tests/support/chunks/pattern.cpp)
set(compute_test_sources tests/integration/compute_binding_test.cpp
    tests/support/render_helpers.hpp tests/support/render_helpers.cpp
    tests/support/first_hit_contract.cpp)
set(mutation_test_sources tests/integration/updates_contract_test.cpp
    tests/support/update_probe.hpp tests/support/update_probe.cpp
    tests/support/bounds_oracle.hpp tests/support/bounds_oracle.cpp
    tests/support/resident_rays.hpp tests/support/resident_rays.cpp)
foreach(test_sources IN ITEMS ray_test_sources generation_test_sources compute_test_sources
                              mutation_test_sources)
    list(APPEND ${test_sources} tests/support/chunks/layout.hpp)
endforeach()
if(ENGINE_DESKTOP)
    get_target_property(engine_shader_inputs rendering_shaders ENGINE_SHADER_INPUTS)
    add_library(rendering_vulkan STATIC ${renderer_sources})
    ac_component(rendering_vulkan rendering.vulkan)
    target_compile_definitions(rendering_vulkan PRIVATE GLFW_INCLUDE_NONE)
    target_include_directories(rendering_vulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(rendering_vulkan
        PUBLIC engine::platform.window engine::world.model engine::motion.camera engine::rendering.settings
        PRIVATE engine::foundation.geometry Vulkan::Vulkan glfw engine_imgui Boost::json)
    ac_nonlink_dependency(rendering_vulkan rendering_shaders runtime)
    add_dependencies(rendering_vulkan engine_shaders)
    add_executable(rendering_contracts_test ${contract_sources})
    ac_test(rendering_contracts_test rendering_vulkan "${CMAKE_CURRENT_BINARY_DIR}/contract.png")
    target_link_libraries(rendering_contracts_test PRIVATE
        engine::rendering.vulkan engine::motion.camera engine::foundation.geometry
        engine::platform.window engine::world.model engine::rendering.settings Vulkan::Vulkan)
    add_executable(rendering_lifecycle_test ${lifecycle_sources})
    ac_register(rendering_lifecycle_test rendering.vulkan TRUE)
    target_link_libraries(rendering_lifecycle_test PRIVATE
        engine::rendering.vulkan engine::motion.camera engine::platform.window
        engine::world.model engine::world.editing engine::foundation.geometry
        engine::rendering.settings engine_imgui)
    add_executable(rendering_frame_wait_test ${frame_wait_sources})
    ac_register(rendering_frame_wait_test rendering.vulkan TRUE)
    target_link_libraries(rendering_frame_wait_test PRIVATE
        engine::rendering.vulkan engine::motion.camera engine::platform.window
        engine::world.model engine::foundation.geometry engine::rendering.settings engine_imgui)
    set(ray_probe_outputs)
    set(helper_probe_outputs)
    foreach(layout IN ITEMS split packed)
        set(output "${CMAKE_CURRENT_BINARY_DIR}")
        set(layout_options)
        if(layout STREQUAL packed)
            string(APPEND output "/packed")
            list(APPEND layout_options -DNAADF_PACKED_CHUNKS=1)
        endif()
        file(MAKE_DIRECTORY "${output}")
        foreach(entry IN ITEMS trace_probe probeProjection probeAtmosphere probeMath)
            if(entry STREQUAL trace_probe)
                set(source "resources/tests/trace_probe.slang")
                set(output_list ray_probe_outputs)
            else()
                set(source "resources/tests/projection_probe.slang")
                set(output_list helper_probe_outputs)
            endif()
            set(probe_binary "${output}/${entry}.spv")
            set(probe_reflection "${output}/${entry}.json")
            add_custom_command(OUTPUT "${probe_binary}" "${probe_reflection}"
                COMMAND "${SLANGC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
                    -I "${ENGINE_SHADER_ROOT}" -entry "${entry}" -stage compute -target spirv
                    -profile spirv_1_5 -matrix-layout-row-major -fvk-use-scalar-layout
                    -fvk-bind-globals 0 0 ${layout_options} -O3
                    -reflection-json "${probe_reflection}" -o "${probe_binary}"
                DEPENDS "${source}" ${engine_shader_inputs} VERBATIM)
            list(APPEND ${output_list} "${probe_binary}" "${probe_reflection}")
        endforeach()
    endforeach()
    set(ray_probe_binary "${CMAKE_CURRENT_BINARY_DIR}/trace_probe.spv")
    set(ray_probe_reflection "${CMAKE_CURRENT_BINARY_DIR}/trace_probe.json")
    add_custom_target(engine_ray_probe DEPENDS ${ray_probe_outputs})
    add_custom_target(engine_helper_probe DEPENDS ${helper_probe_outputs})
    ac_asset(rendering_vulkan resources/tests/trace_probe.slang engine_ray_probe)
    ac_asset(rendering_vulkan resources/tests/projection_probe.slang engine_helper_probe)
    add_executable(rendering_ray_test ${ray_test_sources})
    ac_register(rendering_ray_test rendering.vulkan TRUE)
    target_link_libraries(rendering_ray_test PRIVATE engine::rendering.vulkan
        engine::platform.window engine::world.model engine::foundation.geometry Vulkan::Vulkan)
    add_dependencies(rendering_ray_test engine_ray_probe)
    add_executable(rendering_generation_test ${generation_test_sources})
    ac_register(rendering_generation_test rendering.vulkan TRUE)
    target_link_libraries(rendering_generation_test PRIVATE engine::rendering.vulkan
        engine::platform.window engine::world.model engine::world.storage
        engine::foundation.geometry Vulkan::Vulkan)
    add_executable(rendering_large_world_performance_test ${performance_test_sources})
    ac_register(rendering_large_world_performance_test rendering.vulkan TRUE)
    target_compile_definitions(rendering_large_world_performance_test PRIVATE
        ENGINE_RENDERING_VULKAN_PERFORMANCE_BUILD_TYPE="$<CONFIG>")
    target_link_libraries(rendering_large_world_performance_test PRIVATE
        engine::rendering.vulkan engine::platform.window engine::world.model engine::world.storage
        engine::world.editing
        engine::motion.camera engine::rendering.settings engine::foundation.geometry)
    add_executable(rendering_chunk_layout_performance_test ${chunk_performance_test_sources})
    ac_register(rendering_chunk_layout_performance_test rendering.vulkan TRUE)
    target_link_libraries(rendering_chunk_layout_performance_test PRIVATE
        engine::rendering.vulkan engine::platform.window engine::world.model
        engine::foundation.geometry Vulkan::Vulkan)
    add_dependencies(rendering_chunk_layout_performance_test engine_ray_probe)
    add_executable(rendering_mutation_test ${mutation_test_sources})
    ac_register(rendering_mutation_test rendering.vulkan TRUE)
    target_link_libraries(rendering_mutation_test PRIVATE engine::rendering.vulkan
        engine::platform.window engine::world.model engine::foundation.geometry Vulkan::Vulkan)
    add_dependencies(rendering_mutation_test engine_ray_probe)
    add_executable(rendering_compute_test ${compute_test_sources})
    ac_register(rendering_compute_test rendering.vulkan TRUE)
    target_link_libraries(rendering_compute_test PRIVATE engine::rendering.vulkan
        engine::platform.window engine::world.model engine::foundation.geometry Vulkan::Vulkan)
    add_dependencies(rendering_compute_test engine_helper_probe)
    if(ENGINE_GPU_TESTS)
        add_test(NAME rendering_compute_test COMMAND rendering_compute_test
            "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME rendering_mutation_test COMMAND rendering_mutation_test
            "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME rendering_generation_test COMMAND rendering_generation_test
            "${CMAKE_BINARY_DIR}/shaders")
        add_test(NAME rendering_large_world_performance_test COMMAND
            rendering_large_world_performance_test --shaders "${CMAKE_BINARY_DIR}/shaders"
            --size 64 64 64 --segment-groups 1 --frames 3 --warmup 1 --budget 64
            --width 127 --height 95 --validation)
        add_test(NAME rendering_chunk_layout_performance_test COMMAND
            rendering_chunk_layout_performance_test --shaders "${CMAKE_CURRENT_BINARY_DIR}"
            --size 64 64 64 --rays 1025 --frames 2 --warmup 1 --validation)
        add_test(NAME rendering_ray_test COMMAND rendering_ray_test
            "${ray_probe_binary}" "${ray_probe_reflection}")
        add_test(NAME rendering_lifecycle_test COMMAND rendering_lifecycle_test
            "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_BINARY_DIR}/captures")
        add_test(NAME rendering_frame_wait_test COMMAND rendering_frame_wait_test
            "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_BINARY_DIR}/frame_wait_captures")
    endif()
else()
    add_library(rendering_vulkan INTERFACE)
    ac_component(rendering_vulkan rendering.vulkan)
    ac_asset(rendering_vulkan resources/tests/trace_probe.slang rendering_vulkan)
    ac_asset(rendering_vulkan resources/tests/projection_probe.slang rendering_vulkan)
    engine_inactive_sources(rendering_vulkan "Vulkan adapter requires the desktop build"
        ${renderer_sources} ${contract_sources} ${lifecycle_sources} ${ray_test_sources}
        ${generation_test_sources} ${mutation_test_sources} ${compute_test_sources}
        ${performance_test_sources} ${chunk_performance_test_sources} ${frame_wait_sources})
endif()
