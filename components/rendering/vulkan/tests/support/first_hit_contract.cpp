#include "render_helpers.hpp"

#include "../../src/frame/values.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine::rendering::vulkan::test {
namespace {

constexpr std::uint32_t kPixelCount = 9;
constexpr std::uint32_t kMaterial = 7;

struct FirstHitSnapshot {
    std::vector<std::uint32_t> hits;
    std::vector<std::uint32_t> steps;
    std::vector<std::uint32_t> accumulation;
};

void prepare_first_hit_buffers(WorldCompute& compute) {
    std::array<std::array<std::uint32_t, 4>, kMaterial + 1> materials{};
    materials[kMaterial] = {0, 0x3C003C00U, 0x3C00U, 0};
    compute.upload("voxelTypeData", std::as_bytes(std::span(materials)));
    for (const auto* name : {"blocks", "voxels", "entityVoxelData"}) {
        compute.allocate(name, 4);
    }
    compute.allocate("entityChunkInstances", 20);
    compute.allocate("entityInstancesHistory", 16);
    compute.allocate("atmosphereComp", 12);
    compute.allocate("firstHitData", kPixelCount * 16);
    compute.allocate("rayStepCount", kPixelCount * 4);
    for (const auto* name : {"firstHitAbsorption", "finalColor", "taaSampleAccum"}) {
        compute.allocate(name, kPixelCount * 8);
    }
    compute.allocate("taaSamples", kPixelCount * 8 * 32);
}

void upload_first_hit_plane(WorldCompute& compute, const char* name, const Image& image) {
    const auto command = compute.command();
    image_barrier(command, *image.handle, vk::ImageLayout::eUndefined,
                  vk::ImageLayout::eTransferDstOptimal);
    vk::BufferImageCopy copy;
    copy.setImageSubresource(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1))
        .setImageExtent(image.extent);
    command.copyBufferToImage(*compute.buffers.at(name)->handle, *image.handle,
                              vk::ImageLayout::eTransferDstOptimal, copy);
    image_barrier(command, *image.handle, vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageLayout::eGeneral);
}

void upload_first_hit_chunks(WorldCompute& compute) {
    // One empty chunk followed by a uniform material-7 chunk: every +X ray
    // reaches that material after exactly one empty-space traversal step.
    const std::array<std::uint32_t, 2> geometry{0, 0x40000000U | kMaterial};
    const std::array<std::uint32_t, 2> entities{};
    compute.chunks = make_chunk_images(compute.device, {2, 1, 1});
    if (compute.chunks.packed()) {
        const std::array<std::uint32_t, 4> packed{geometry[0], 0, geometry[1], 0};
        compute.upload("first_hit_chunks", std::as_bytes(std::span(packed)));
    } else {
        compute.upload("first_hit_chunks", std::as_bytes(std::span(geometry)));
        compute.upload("first_hit_entities", std::as_bytes(std::span(entities)));
        upload_first_hit_plane(compute, "first_hit_entities", *compute.chunks.entities);
    }
    upload_first_hit_plane(compute, "first_hit_chunks", *compute.chunks.geometry);
}

UniformValues first_hit_values(bool show_steps, float direction = 1) {
    UniformValues values;
    Matrix ray_transform{};
    ray_transform[12] = direction;
    ray_transform[15] = 1;
    set_value(values, "invCamMatrix", ray_transform);
    set_value(values, "camPosIntX", 0);
    set_value(values, "camPosIntY", 8);
    set_value(values, "camPosIntZ", 8);
    set_value(values, "camPosFrac", std::array<float, 3>{0.5F, 0.5F, 0.5F});
    set_value(values, "boundingBoxMin", std::array<float, 3>{0.1F, 0.1F, 0.1F});
    set_value(values, "boundingBoxMax", std::array<float, 3>{31.9F, 15.9F, 15.9F});
    set_value(values, "screenWidth", 3U);
    set_value(values, "screenHeight", 3U);
    set_value(values, "randCounter", 2026U);
    set_value(values, "skySunDir", std::array<float, 3>{0, 1, 0});
    set_value(values, "isTAA", 1U);
    set_value(values, "showRayStep", static_cast<std::uint32_t>(show_steps));
    set_value(values, "atmosphereTexSizeX", 1);
    set_value(values, "atmosphereTexSizeY", 1);
    return values;
}

std::vector<std::uint32_t> read_words(WorldCompute& compute, const std::string& name,
                                      std::uint32_t count) {
    const auto bytes = compute.read(name, 0, count * sizeof(std::uint32_t));
    std::vector<std::uint32_t> words(count);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

FirstHitSnapshot capture_first_hit(WorldCompute& compute, const std::string& program,
                                   bool show_steps) {
    compute.command().fillBuffer(*compute.buffers.at("rayStepCount")->handle, 0, kPixelCount * 4,
                                 0xFFFFFFFFU);
    memory_barrier(compute.command());
    compute.run(program, {1, 1, 1}, first_hit_values(show_steps));
    return {read_words(compute, "firstHitData", kPixelCount * 4),
            read_words(compute, "rayStepCount", kPixelCount),
            read_words(compute, "taaSampleAccum", kPixelCount * 2)};
}

void verify_first_hit_program(WorldCompute& compute, const std::string& program, bool albedo) {
    const auto ordinary = capture_first_hit(compute, program, false);
    const auto diagnostic = capture_first_hit(compute, program, true);
    for (std::size_t pixel = 0; pixel < kPixelCount; ++pixel) {
        if (ordinary.steps[pixel] != 0xFFFFFFFFU) {
            throw std::runtime_error("Disabled ray-step display wrote diagnostic storage: " +
                                     program);
        }
        if ((ordinary.hits[pixel * 4 + 2] & 0x7FFFU) != kMaterial ||
            (diagnostic.hits[pixel * 4 + 2] & 0x7FFFU) != kMaterial ||
            diagnostic.steps[pixel] != 1) {
            throw std::runtime_error("Ray-step display changed production hit material: " +
                                     program);
        }
        if (albedo && ((ordinary.accumulation[pixel * 2] & 0xFFFFU) != 0x3C00U ||
                       (diagnostic.accumulation[pixel * 2] & 0xFFFFU) != 0x3C00U)) {
            throw std::runtime_error("Ray-step display overwrote the Albedo accumulation weight");
        }
    }
}

void verify_sky_result(WorldCompute& compute) {
    const auto hits = read_words(compute, "firstHitData", kPixelCount * 4);
    const auto colors = read_words(compute, "finalColor", kPixelCount * 2);
    const auto samples = read_words(compute, "sampleAccumulated", kPixelCount * 4);
    constexpr std::array kExpected{0.75F, 1.5F, 2.25F, 2.0F};
    for (std::size_t pixel = 0; pixel < kPixelCount; ++pixel) {
        if ((hits[pixel * 4] >> 15) != 0x1FFFFU || (hits[pixel * 4 + 2] & 0x7FFFU) != 0 ||
            colors[pixel * 2] != 0x40003C00U || colors[pixel * 2 + 1] != 0x4200U) {
            throw std::runtime_error("PathTracer sky fixture did not produce an empty first hit");
        }
        for (std::size_t channel = 0; channel < kExpected.size(); ++channel) {
            const auto value = std::bit_cast<float>(samples[pixel * 4 + channel]);
            if (!std::isfinite(value) || std::abs(value - kExpected[channel]) > 1e-6F) {
                throw std::runtime_error("PathTracer GI corrupted the sky running average");
            }
        }
    }
}

void verify_sky_accumulation(WorldCompute& compute) {
    const std::array<std::uint32_t, 3> atmosphere{0x40003C00U, 0x3C004200U, 0x3C003C00U};
    compute.upload("atmosphereComp", std::as_bytes(std::span(atmosphere)));
    std::array<std::array<float, 4>, kPixelCount> samples;
    samples.fill({0.5F, 1, 1.5F, 1});
    compute.upload("sampleAccumulated", std::as_bytes(std::span(samples)));
    auto values = first_hit_values(false, -1);
    set_value(values, "maxSamples", 8);
    set_value(values, "taaIndex", 0);
    compute.run("pathTracer_renderFirstHit_calcFirstHit", {1, 1, 1}, values);
    compute.run("pathTracer_renderGlobalIllum_calcGlobalIlum", {1, 1, 1}, values);
    verify_sky_result(compute);
}

} // namespace

void verify_first_hit_data(WorldCompute& compute) {
    prepare_first_hit_buffers(compute);
    upload_first_hit_chunks(compute);
    verify_first_hit_program(compute, "base_renderFirstHit_calcFirstHit", false);
    verify_first_hit_program(compute, "albedo_renderFirstHit_calcFirstHit", true);
    verify_first_hit_program(compute, "pathTracer_renderFirstHit_calcFirstHit", false);
    verify_sky_accumulation(compute);
    std::cout << "All three production FirstHit entries preserve material 7 with ray-step "
                 "display off/on; disabled diagnostic storage is untouched, enabled count is 1, "
                 "and Albedo sample weight stays 1; "
                 "PathTracer sky GI preserves the finite running average\n";
}

} // namespace engine::rendering::vulkan::test
