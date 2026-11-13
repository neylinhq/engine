set(app_sources
    src/main.cpp src/options.hpp src/options.cpp src/application.hpp src/application.cpp
    src/navigation.cpp src/editing.cpp src/entities.cpp src/requests.cpp src/frame_context.cpp
    src/io/settings.cpp
    src/io/path.hpp src/io/executable.hpp src/io/executable.cpp)
set(app_test_sources tests/unit/options_test.cpp)
if(ENGINE_DESKTOP)
    add_executable(sandbox ${app_sources})
    ac_application(sandbox app.sandbox)
    if(MINGW)
        target_link_options(sandbox PRIVATE -municode)
    endif()
    target_link_libraries(sandbox PRIVATE
        engine::foundation.geometry engine::motion.camera engine::motion.path
        engine::interaction.input engine::platform.window engine::world.model engine::world.editing
        engine::world.storage engine::rendering.settings engine::rendering.vulkan engine::gui.frontend
        engine::rendering.configuration engine::motion.character engine::world.collision
        engine::platform.process Boost::json)
    target_compile_definitions(sandbox PRIVATE
        ENGINE_APP_SANDBOX_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        ENGINE_APP_SANDBOX_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        ENGINE_APP_SANDBOX_VERSION="${PROJECT_VERSION}")
    add_dependencies(sandbox engine_runtime_shaders)
    install(TARGETS sandbox RUNTIME DESTINATION bin)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION bin/shaders)
    install(FILES docs/runtime.md DESTINATION . RENAME README.md)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION .)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/licenses/" DESTINATION licenses)
    install(DIRECTORY "${physics_LICENSE_DIR}/" DESTINATION licenses/physics)
    set(notices_source "${PROJECT_SOURCE_DIR}/docs/third_party_notices.md")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${notices_source}")
    file(READ "${notices_source}" notices)
    string(REPLACE "](../licenses/" "](licenses/" installed_notices "${notices}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_NOTICES.md"
        CONTENT "${installed_notices}" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION .)
    add_executable(options_test src/options.cpp src/options.hpp src/io/path.hpp
        src/io/executable.hpp src/io/executable.cpp tests/unit/options_test.cpp)
    ac_test(options_test sandbox)
    target_link_libraries(options_test PRIVATE engine::rendering.settings engine::rendering.vulkan)
else()
    add_library(sandbox INTERFACE)
    ac_application(sandbox app.sandbox)
    engine_inactive_sources(sandbox "Sandbox requires the desktop build"
        ${app_sources} ${app_test_sources})
endif()
