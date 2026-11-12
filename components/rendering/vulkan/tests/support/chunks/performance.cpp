#include "performance.hpp"

#include "../../../src/frame/values.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace engine::rendering::vulkan::test {
namespace {

using Ray = std::array<float, 8>;
using RayRecord = std::array<std::uint32_t, 8>;

platform::window::Window create_window() {
    auto result = platform::window::Window::create({32, 32, "Chunk layout diagnostic", false});
    if (!result) {
        throw std::runtime_error(result.error());
    }
    return std::move(*result);
}

float random_unit(std::uint32_t& state) {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return static_cast<float>(state >> 8U) * (1.0F / 16777216.0F);
}

std::vector<Ray> make_rays(const ChunkPerformanceOptions& options) {
    std::vector<Ray> rays(options.rays);
    auto state = kChunkRaySeed;
    for (auto& ray : rays) {
        float direction_length = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            ray[axis] = 0.5F + random_unit(state) * static_cast<float>(options.size[axis] - 1U);
            ray[axis + 4] = 2.0F * random_unit(state) - 1.0F;
            direction_length += ray[axis + 4] * ray[axis + 4];
        }
        if (direction_length < 0.000001F) {
            ray[4] = 1;
        }
    }
    return rays;
}

std::uint64_t fingerprint(std::span<const Ray> rays) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : std::as_bytes(rays)) {
        value = (value ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    return value;
}

void validate_split_image(vk::PhysicalDevice physical, vk::Extent3D extent) {
    const auto usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                       vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    const auto limits = physical.getImageFormatProperties(vk::Format::eR32Uint, vk::ImageType::e3D,
                                                          vk::ImageTiling::eOptimal, usage, {});
    const auto bytes = std::uint64_t(extent.width) * extent.height * extent.depth * 4U;
    if (extent.width > limits.maxExtent.width || extent.height > limits.maxExtent.height ||
        extent.depth > limits.maxExtent.depth || bytes > limits.maxResourceSize) {
        throw std::runtime_error("Requested synthetic chunk volume exceeds R32 image limits");
    }
}

} // namespace

ChunkLayoutPerformance::ChunkLayoutPerformance(ChunkPerformanceOptions options)
    : options_(std::move(options)), window_(create_window()),
      device_(window_, options_.validation, options_.layout),
      compute_(device_, options_.shaders, buffers_, chunks_) {
    validate_limits();
    timestamps_ = device_.logical->createQueryPoolUnique({{}, vk::QueryType::eTimestamp, 2});
    prepare();
}

void ChunkLayoutPerformance::validate_limits() const {
    const auto& limits = device_.properties.limits;
    for (const auto dimension : options_.size) {
        if (dimension < 16 || dimension % 16 != 0 || dimension / 16 > limits.maxImageDimension3D) {
            throw std::runtime_error("Voxel dimensions must be whole chunks supported by this GPU");
        }
    }
    const auto groups = (std::uint64_t(options_.rays) + 63U) / 64U;
    const auto bytes = std::uint64_t(options_.rays) * sizeof(Ray);
    if (options_.rays == 0 || groups > limits.maxComputeWorkGroupCount[0] ||
        bytes > limits.maxStorageBufferRange || bytes > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Ray count exceeds GPU dispatch, buffer or host address limits");
    }
    if (device_.timestamp_bits == 0 || !std::isfinite(limits.timestampPeriod) ||
        limits.timestampPeriod <= 0) {
        throw std::runtime_error("This GPU queue does not provide usable timestamps");
    }
    if (!device_.packed_chunks) {
        validate_split_image(device_.physical,
                             {options_.size[0] / 16, options_.size[1] / 16, options_.size[2] / 16});
    }
}

void ChunkLayoutPerformance::prepare() {
    const auto& size = options_.size;
    chunks_ = make_chunk_images(device_, {size[0] / 16, size[1] / 16, size[2] / 16});
    pattern_info_ = initialize_chunk_pattern(compute_, options_.pattern);
    for (const auto* name : {"blocks", "voxels", "entityVoxelData"}) {
        compute_.allocate(name, 4);
    }
    compute_.allocate("voxelTypeData", 16);
    compute_.allocate("entityChunkInstances", 20);
    compute_.allocate("entityInstancesHistory", 16);
    const auto rays = make_rays(options_);
    ray_fingerprint_ = fingerprint(rays);
    compute_.upload("test_rays", std::as_bytes(std::span(rays)));
    compute_.allocate("test_results", std::uint64_t(options_.rays) * sizeof(RayRecord));
    set_value(values_, "ray_count", options_.rays);
    set_value(values_, "boundingBoxMin", std::array<float, 3>{0.1F, 0.1F, 0.1F});
    set_value(
        values_, "boundingBoxMax",
        std::array<float, 3>{float(size[0]) - 0.1F, float(size[1]) - 0.1F, float(size[2]) - 0.1F});
    compute_.synchronize();
}

double ChunkLayoutPerformance::dispatch(bool measured) {
    const auto command = compute_.command();
    if (measured) {
        command.resetQueryPool(*timestamps_, 0, 2);
        command.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestamps_, 0);
    }
    const auto groups = static_cast<std::uint32_t>((std::uint64_t(options_.rays) + 63U) / 64U);
    compute_.run("trace_probe", {groups, 1, 1}, values_);
    if (measured) {
        command.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *timestamps_, 1);
    }
    compute_.synchronize();
    if (!measured) {
        return 0;
    }
    std::array<std::uint64_t, 2> values{};
    const auto result =
        vkGetQueryPoolResults(*device_.logical, *timestamps_, 0, 2, sizeof(values), values.data(),
                              sizeof(values.front()), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Completed trace timestamps were unavailable");
    }
    const auto mask = device_.timestamp_bits >= 64
                          ? ~std::uint64_t{0}
                          : (std::uint64_t{1} << device_.timestamp_bits) - 1;
    return double((values[1] - values[0]) & mask) * device_.properties.limits.timestampPeriod *
           1e-6;
}

std::uint64_t ChunkLayoutPerformance::verify_results() {
    const auto bytes =
        compute_.read("test_results", 0, std::uint64_t(options_.rays) * sizeof(RayRecord));
    constexpr RayRecord kExpected{0, 0, std::bit_cast<std::uint32_t>(100000000.0F), 0x3FFFU, 0, 0,
                                  0, 0};
    for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(RayRecord)) {
        RayRecord actual{};
        std::memcpy(actual.data(), bytes.data() + offset, sizeof(actual));
        if (actual != kExpected) {
            throw std::runtime_error("Synthetic empty-volume ray did not return the expected miss");
        }
    }
    return bytes.size() / sizeof(RayRecord);
}

std::uint64_t ChunkLayoutPerformance::allocated_bytes() const {
    auto bytes = chunks_.allocation() + compute_.allocated_bytes();
    for (const auto& [name, buffer] : buffers_) {
        static_cast<void>(name);
        bytes += buffer->allocation;
    }
    return bytes;
}

ChunkPerformanceResult ChunkLayoutPerformance::run() {
    ChunkPerformanceResult result;
    result.gpu_milliseconds.resize(options_.frames);
    for (unsigned frame = 0; frame < options_.warmup; ++frame) {
        static_cast<void>(dispatch(false));
    }
    for (auto& milliseconds : result.gpu_milliseconds) {
        milliseconds = dispatch(true);
    }
    result.verified_misses = verify_results();
    if (result.verified_misses != options_.rays || device_.validation_errors.load() != 0) {
        throw std::runtime_error("Synthetic trace result count or Vulkan validation failed");
    }
    result.image_bytes = chunks_.allocation();
    result.allocated_bytes = allocated_bytes();
    result.ray_fingerprint = ray_fingerprint_;
    result.binding_allocations = compute_.binding_allocations();
    result.pattern = pattern_info_;
    return result;
}

const Device& ChunkLayoutPerformance::device() const {
    return device_;
}

} // namespace engine::rendering::vulkan::test
