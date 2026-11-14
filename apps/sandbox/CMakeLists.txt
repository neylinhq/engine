set(app_sources
    src/options.hpp src/options.cpp src/application.hpp src/application.cpp
    src/navigation.cpp src/editing.cpp src/entities.cpp src/requests.cpp src/frame_context.cpp
    src/io/materials.cpp src/io/settings.cpp
    src/io/path.hpp src/io/executable.hpp src/io/executable.cpp)
set(app_test_sources tests/unit/options_test.cpp)
set(palette_test_source tests/integration/palette_reopen_test.cpp)
if(ENGINE_DESKTOP)
    set(sandbox_dependencies
        engine::foundation.geometry engine::motion.camera engine::motion.path
        engine::interaction.input engine::platform.window engine::world.model engine::world.editing
        engine::world.storage engine::rendering.settings engine::rendering.vulkan engine::gui.frontend
        engine::rendering.configuration engine::motion.character engine::world.collision
        engine::platform.process Boost::json)
    set(sandbox_build_definitions
        ENGINE_APP_SANDBOX_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        ENGINE_APP_SANDBOX_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        ENGINE_APP_SANDBOX_VERSION="${PROJECT_VERSION}")
    add_executable(sandbox src/main.cpp ${app_sources})
    ac_application(sandbox app.sandbox)
    if(MINGW)
        target_link_options(sandbox PRIVATE -municode)
    endif()
    target_link_libraries(sandbox PRIVATE ${sandbox_dependencies})
    target_compile_definitions(sandbox PRIVATE ${sandbox_build_definitions})
    add_dependencies(sandbox engine_runtime_shaders)
    install(TARGETS sandbox RUNTIME DESTINATION bin)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION bin/shaders)
    set(runtime_doc "${CMAKE_CURRENT_SOURCE_DIR}/docs/runtime.md")
    set(palette_doc "${PROJECT_SOURCE_DIR}/components/world/storage/README.md")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${runtime_doc}")
    file(READ "${runtime_doc}" runtime_readme)
    string(REPLACE "../../../components/world/storage/README.md" "docs/world_storage.md"
        installed_runtime_readme "${runtime_readme}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/README.md"
        CONTENT "${installed_runtime_readme}" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/README.md" DESTINATION .)
    install(FILES "${palette_doc}" DESTINATION docs RENAME world_storage.md)
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
    add_executable(sandbox_palette_reopen_test ${palette_test_source} ${app_sources})
    ac_register(sandbox_palette_reopen_test app.sandbox TRUE)
    target_link_libraries(sandbox_palette_reopen_test PRIVATE ${sandbox_dependencies})
    target_compile_definitions(sandbox_palette_reopen_test PRIVATE ${sandbox_build_definitions})
    add_dependencies(sandbox_palette_reopen_test engine_runtime_shaders)
    if(ENGINE_GPU_TESTS)
        add_test(NAME sandbox_palette_reopen_test COMMAND sandbox_palette_reopen_test
            "${CMAKE_BINARY_DIR}/shaders" "${CMAKE_CURRENT_BINARY_DIR}/palette_test"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/duplicate_material_names.cvox")
    endif()
else()
    add_library(sandbox INTERFACE)
    ac_application(sandbox app.sandbox)
    engine_inactive_sources(sandbox "Sandbox requires the desktop build"
        src/main.cpp ${app_sources} ${app_test_sources} ${palette_test_source})
endif()
if(TARGET sandbox_palette_reopen_test)
    engine_assets(sandbox sandbox_palette_reopen_test tests/fixtures)
else()
    engine_assets(sandbox sandbox tests/fixtures)
endif()
