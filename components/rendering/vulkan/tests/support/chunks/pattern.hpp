#ifndef ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_CHUNKS_PATTERN_HPP
#define ENGINE_RENDERING_VULKAN_TESTS_SUPPORT_CHUNKS_PATTERN_HPP

#include "../../../src/world/compute.hpp"

#include <cstdint>

namespace engine::rendering::vulkan::test {

enum class ChunkPattern { uniform, varied };

struct ChunkPatternInfo {
    std::uint64_t geometry_fingerprint = 0;
    std::uint64_t staging_payload_bytes = 0;
    std::uint64_t staging_allocation_bytes = 0;
    std::uint32_t verified_samples = 0;
};

[[nodiscard]] ChunkPatternInfo initialize_chunk_pattern(WorldCompute& compute,
                                                        ChunkPattern pattern);

} // namespace engine::rendering::vulkan::test

#endif
