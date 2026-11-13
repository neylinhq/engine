cmake_minimum_required(VERSION 4.4.5)

find_package(Git 2.49 REQUIRED)
find_program(CONAN_EXECUTABLE NAMES conan REQUIRED)

set(physics_repository "git@github.com:neylinhq/physics.git")
set(physics_commit "42e3f4f82b541968f0a87f7314c4a50b04648270") # v0.1.0
get_filename_component(engine_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(dependencies_dir "${engine_root}/out/dependencies")
set(physics_source_dir "${dependencies_dir}/physics-${physics_commit}")

file(MAKE_DIRECTORY "${dependencies_dir}")
file(LOCK "${dependencies_dir}/physics.lock" GUARD PROCESS TIMEOUT 60)
if(NOT EXISTS "${physics_source_dir}/.git")
    # Resolve Git URL rewrites from the Engine checkout before cloning.
    execute_process(COMMAND "${GIT_EXECUTABLE}" ls-remote --get-url "${physics_repository}"
        WORKING_DIRECTORY "${engine_root}"
        OUTPUT_VARIABLE physics_url OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" clone --depth=1 "--revision=${physics_commit}"
        -- "${physics_url}" "${physics_source_dir}"
        COMMAND_ERROR_IS_FATAL ANY)
endif()

execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${physics_source_dir}" rev-parse HEAD
    OUTPUT_VARIABLE physics_head OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${physics_source_dir}" status --porcelain --untracked-files=all
    OUTPUT_VARIABLE physics_changes
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT physics_head STREQUAL physics_commit OR NOT physics_changes STREQUAL "")
    message(FATAL_ERROR "Expected a clean Physics checkout at ${physics_commit} in ${physics_source_dir}")
endif()
execute_process(COMMAND "${CONAN_EXECUTABLE}" export "${physics_source_dir}"
    WORKING_DIRECTORY "${engine_root}"
    COMMAND_ERROR_IS_FATAL ANY)
