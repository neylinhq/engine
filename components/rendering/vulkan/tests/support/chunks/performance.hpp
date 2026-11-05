#pragma once

#include "pattern.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace engine::rendering::vulkan::test {

struct ChunkPerformanceOptions {
    std::filesystem::path shaders = "shaders";
    std::array<std::uint32_t, 3> size{4096, 512, 4096};
    std::uint32_t rays = 65536;
    unsigned frames = 16;
    unsigned warmup = 4;
    ChunkLayout layout = ChunkLayout::split;
    ChunkPattern pattern = ChunkPattern::uniform;
    bool validation = false;
    bool help = false;
};

struct ChunkPerformanceResult {
    std::vector<double> gpu_milliseconds;
    std::uint64_t image_bytes = 0;
    std::uint64_t allocated_bytes = 0;
    std::uint64_t ray_fingerprint = 0;
    std::uint64_t verified_misses = 0;
    std::uint64_t binding_allocations = 0;
    ChunkPatternInfo pattern;
};

inline constexpr std::uint32_t kChunkRaySeed = 0x6E616164U;

class ChunkLayoutPerformance {
  public:
    explicit ChunkLayoutPerformance(ChunkPerformanceOptions options);
    [[nodiscard]] ChunkPerformanceResult run();
    [[nodiscard]] const Device& device() const;

  private:
    ChunkPerformanceOptions options_;
    platform::window::Window window_;
    Device device_;
    BufferMap buffers_;
    ChunkImages chunks_;
    vk::UniqueQueryPool timestamps_;
    WorldCompute compute_;
    UniformValues values_;
    std::uint64_t ray_fingerprint_ = 0;
    ChunkPatternInfo pattern_info_;

    void validate_limits() const;
    void prepare();
    [[nodiscard]] double dispatch(bool measured);
    [[nodiscard]] std::uint64_t verify_results();
    [[nodiscard]] std::uint64_t allocated_bytes() const;
};

} // namespace engine::rendering::vulkan::test
