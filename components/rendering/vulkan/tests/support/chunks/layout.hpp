#pragma once

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
