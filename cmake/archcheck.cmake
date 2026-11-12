set(ENGINE_ARCHCHECK_ROOT "$ENV{ENGINE_ARCHCHECK_ROOT}" CACHE PATH "Installed archcheck prefix")
find_file(ENGINE_ARCHCHECK_EXPORTER
    NAMES architecture.cmake
    HINTS "${ENGINE_ARCHCHECK_ROOT}"
    PATH_SUFFIXES share/archcheck/cmake
    DOC "Installed archcheck CMake exporter"
    REQUIRED)
include("${ENGINE_ARCHCHECK_EXPORTER}")
set(ARCHCHECK_SDK engine)

function(engine_inactive_sources target reason)
    foreach(source IN LISTS ARGN)
        ac_inactive("${target}" "${source}" "${reason}" PLATFORMS windows linux)
    endforeach()
endfunction()

function(engine_assets owner handler directory)
    file(GLOB_RECURSE assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${directory}/*")
    foreach(asset IN LISTS assets)
        ac_asset("${owner}" "${asset}" "${handler}")
    endforeach()
endfunction()
