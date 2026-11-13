cmake_minimum_required(VERSION 4.4.5)
string(RANDOM LENGTH 12 token)
set(prefix "${BUILD_DIR}/installed-test/${token}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}"
    COMMAND_ERROR_IS_FATAL ANY)

foreach(notice IN ITEMS LICENSE box3d.txt qsort.txt)
    if(NOT EXISTS "${prefix}/licenses/physics/${notice}")
        message(FATAL_ERROR "Missing installed Physics SDK notice: ${notice}")
    endif()
endforeach()

file(READ "${prefix}/bin/shaders/programs.json" manifest)
string(JSON program_count LENGTH "${manifest}" programs)
if(program_count EQUAL 0)
    message(FATAL_ERROR "The installed shader manifest contains no programs")
endif()
math(EXPR last_program "${program_count} - 1")
foreach(index RANGE "${last_program}")
    string(JSON name GET "${manifest}" programs "${index}" name)
    foreach(layout IN ITEMS shaders shaders/packed)
        foreach(extension IN ITEMS spv json)
            set(artifact "${prefix}/bin/${layout}/${name}.${extension}")
            if(NOT EXISTS "${artifact}")
                message(FATAL_ERROR "Missing installed shader artifact: ${artifact}")
            endif()
        endforeach()
    endforeach()
endforeach()

execute_process(COMMAND "${prefix}/bin/sandbox${EXECUTABLE_SUFFIX}" --help
    WORKING_DIRECTORY "${prefix}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${prefix}/bin/benchmark${EXECUTABLE_SUFFIX}"
    validate --scenario benchmark/scenarios/smoke.json
    WORKING_DIRECTORY "${prefix}" COMMAND_ERROR_IS_FATAL ANY)
