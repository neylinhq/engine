add_library(world_storage STATIC
    src/cvox.cpp
    src/path.cpp
    src/palette.cpp
    src/vl32.cpp
    src/vox_read.cpp
    src/vox_scene.cpp
    src/mesh.cpp
    src/obj.cpp
    src/stl.cpp
    src/mesh/clipping.cpp
    src/mesh/clipping.hpp
    src/mesh/raster.cpp
    src/mesh/raster.hpp
    src/codec/binary.cpp
    src/codec/zip.cpp
    src/codec/inflate.cpp
    src/codec/deflate.cpp
    src/image/image.cpp
    src/image/bmp.cpp
    src/image/png.cpp
    src/image/gif.cpp
    src/jpeg/entropy.cpp
    src/jpeg/tables.cpp
    src/jpeg/frame.cpp
    src/jpeg/scan_header.cpp
    src/jpeg/scans.cpp
    src/jpeg/progressive.cpp
    src/jpeg/jpeg.cpp
    src/jpeg/reconstruct.cpp
    src/jpeg/decoder.hpp
    src/import.hpp
    src/vox.hpp
    src/codec/binary.hpp
    src/codec/zip.hpp
    src/image/image.hpp
    include/engine/world/storage/storage.hpp
)
ac_component(world_storage world.storage)
target_include_directories(world_storage PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(world_storage PUBLIC engine::world.model engine::foundation.geometry)
add_executable(world_storage_test tests/unit/storage_test.cpp)
ac_test(world_storage_test world_storage)
target_link_libraries(world_storage_test PRIVATE engine::world.storage engine::world.model engine::foundation.geometry)
add_executable(jpeg_test tests/unit/jpeg_test.cpp)
ac_test(jpeg_test world_storage)
target_link_libraries(jpeg_test PRIVATE engine::world.storage)
add_executable(world_import_test tests/unit/import_test.cpp)
ac_test(world_import_test world_storage)
target_link_libraries(world_import_test PRIVATE engine::world.storage engine::world.model engine::foundation.geometry)
target_compile_definitions(world_import_test PRIVATE
    ENGINE_WORLD_STORAGE_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/import")
add_executable(world_model_benchmark benchmarks/world_benchmark.cpp)
ac_test(world_model_benchmark world_storage)
target_link_libraries(world_model_benchmark PRIVATE engine::world.storage engine::world.editing
    engine::world.model engine::foundation.geometry)
add_executable(palette_test tests/unit/palette_test.cpp)
ac_test(palette_test world_storage)
target_link_libraries(palette_test PRIVATE engine::world.storage engine::world.model
    engine::foundation.geometry)
add_executable(jpeg_malformed_test tests/unit/jpeg_malformed_test.cpp)
ac_test(jpeg_malformed_test world_storage)
target_link_libraries(jpeg_malformed_test PRIVATE engine::world.storage)
