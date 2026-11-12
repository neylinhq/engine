#ifndef ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_CHUNKS_LAYOUT_HPP
#define ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_CHUNKS_LAYOUT_HPP

#include <engine/rendering/vulkan/renderer.hpp>

#include <stdexcept>
#include <string_view>

namespace engine::rendering::vulkan::test {

inline ChunkLayout parse_chunk_layout(std::string_view flag) {
    if (flag == "--packed") {
        return ChunkLayout::packed;
    }
    if (flag == "--split") {
        return ChunkLayout::split;
    }
    throw std::runtime_error("Chunk layout must be --packed or --split");
}

} // namespace engine::rendering::vulkan::test

#endif
