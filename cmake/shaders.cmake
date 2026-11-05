include_guard(GLOBAL)
find_program(SLANGC_EXECUTABLE slangc REQUIRED)
set(ENGINE_SHADER_ROOT "${PROJECT_SOURCE_DIR}/components/rendering/shaders/resources/shaders")
file(GLOB_RECURSE engine_shader_inputs CONFIGURE_DEPENDS "${ENGINE_SHADER_ROOT}/*.slang")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
function(engine_shader name source entry stage)
    foreach(layout IN ITEMS split packed)
        set(output "${CMAKE_BINARY_DIR}/shaders")
        set(layout_options)
        if(layout STREQUAL packed)
            string(APPEND output "/packed")
            list(APPEND layout_options -DNAADF_PACKED_CHUNKS=1)
        endif()
        file(MAKE_DIRECTORY "${output}")
        set(binary "${output}/${name}.spv")
        set(reflection "${output}/${name}.json")
        add_custom_command(OUTPUT "${binary}" "${reflection}"
            COMMAND "${SLANGC_EXECUTABLE}" "${ENGINE_SHADER_ROOT}/${source}"
                -entry "${entry}" -stage "${stage}" -target spirv -profile spirv_1_5
                -matrix-layout-row-major -fvk-use-scalar-layout -fvk-bind-globals 0 0
                ${layout_options} -O3 -reflection-json "${reflection}" -o "${binary}"
            DEPENDS ${engine_shader_inputs} VERBATIM)
        set_property(GLOBAL APPEND PROPERTY ENGINE_SHADER_OUTPUTS "${binary}" "${reflection}")
    endforeach()
endfunction()
include("${PROJECT_SOURCE_DIR}/cmake/shader_entries.cmake")
get_property(engine_shader_outputs GLOBAL PROPERTY ENGINE_SHADER_OUTPUTS)
add_custom_target(engine_shaders ALL DEPENDS ${engine_shader_outputs})
add_custom_target(engine_runtime_shaders
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
    VERBATIM)
add_dependencies(engine_runtime_shaders engine_shaders)
configure_file("${PROJECT_SOURCE_DIR}/components/rendering/shaders/resources/programs.json"
    "${CMAKE_BINARY_DIR}/shaders/programs.json" COPYONLY)
