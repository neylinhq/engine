add_library(motion_character STATIC
    src/motor.cpp src/contacts.cpp src/contacts.hpp
    include/engine/motion/character/collision.hpp
    include/engine/motion/character/motor.hpp)
ac_component(motion_character motion.character)
target_include_directories(motion_character PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(motion_character PUBLIC engine::foundation.geometry)
add_executable(character_test tests/unit/character_test.cpp)
ac_test(character_test motion_character)
target_link_libraries(character_test PRIVATE engine::motion.character engine::foundation.geometry)
