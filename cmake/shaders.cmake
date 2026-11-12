include_guard(GLOBAL)
find_program(SLANGC_EXECUTABLE slangc REQUIRED)
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
        set_property(TARGET rendering_shaders APPEND PROPERTY ENGINE_SHADER_OUTPUTS
            "${binary}" "${reflection}")
    endforeach()
endfunction()
configure_file(resources/programs.json
    "${CMAKE_BINARY_DIR}/shaders/programs.json" COPYONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/programs.json" shader_manifest)
string(JSON program_count LENGTH "${shader_manifest}" programs)
if(program_count EQUAL 0)
    message(FATAL_ERROR "The shader manifest contains no programs")
endif()
math(EXPR last_program "${program_count} - 1")
foreach(index RANGE "${last_program}")
    string(JSON program GET "${shader_manifest}" programs "${index}")
    foreach(field IN ITEMS name source entry stage)
        string(JSON "${field}" GET "${program}" "${field}")
    endforeach()
    engine_shader("${name}" "${source}" "${entry}" "${stage}")
endforeach()
get_target_property(engine_shader_outputs rendering_shaders ENGINE_SHADER_OUTPUTS)
add_custom_target(engine_shaders ALL DEPENDS ${engine_shader_outputs})
add_custom_target(engine_runtime_shaders
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
    VERBATIM)
add_dependencies(engine_runtime_shaders engine_shaders)
