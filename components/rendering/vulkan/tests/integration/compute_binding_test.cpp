#include "../../src/frame/values.hpp"
#include "../../src/world/compute.hpp"
#include "../support/chunks/layout.hpp"
#include "../support/render_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
namespace renderer = engine::rendering::vulkan;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

renderer::ChunkLayout chunk_layout(int argc, char** argv) {
    if (argc == 3) {
        return renderer::ChunkLayout::automatic;
    }
    return renderer::test::parse_chunk_layout(argv[3]);
}

renderer::UniformValues copy_values(unsigned source, unsigned destination, unsigned count) {
    renderer::UniformValues values;
    renderer::set_value(values, "offsetSrc", source);
    renderer::set_value(values, "offsetDst", destination);
    renderer::set_value(values, "count", count);
    return values;
}

void check_words(renderer::WorldCompute& compute, std::span<const std::uint32_t> expected) {
    const auto bytes = compute.read("dstData", 0, expected.size_bytes());
    require(bytes.size() == expected.size_bytes(), "Readback size changed");
    require(std::memcmp(bytes.data(), expected.data(), bytes.size()) == 0,
            "Pending dispatch bindings, uniforms or buffer growth corrupted data");
}

void pending_dispatches(renderer::WorldCompute& compute, std::array<std::uint32_t, 384>& expected) {
    std::array<std::uint32_t, 64> first{}, second{};
    for (unsigned index = 0; index < 64; ++index) {
        first[index] = 100 + index;
        second[index] = 1000 + index;
    }
    compute.upload("source_a", std::as_bytes(std::span(first)));
    compute.upload("source_b", std::as_bytes(std::span(second)));
    compute.allocate("dstData", 64 * 4);
    std::weak_ptr<renderer::Buffer> retired = compute.buffers.at("dstData");
    compute.buffers["srcData"] = compute.buffers.at("source_a");
    compute.run("_dataCopy_copyData", {1, 1, 1}, copy_values(2, 0, 16));
    compute.buffers["srcData"] = compute.buffers.at("source_b");
    compute.run("_dataCopy_copyData", {1, 1, 1}, copy_values(3, 16, 17));
    compute.grow("dstData", 128 * 4);
    compute.run("_dataCopy_copyData", {1, 1, 1}, copy_values(40, 96, 8));
    require(!retired.expired(), "A pending dispatch lost its replaced destination buffer");
    std::copy_n(first.begin() + 2, 16, expected.begin());
    std::copy_n(second.begin() + 3, 17, expected.begin() + 16);
    std::copy_n(second.begin() + 40, 8, expected.begin() + 96);
    check_words(compute, std::span(expected).first(128));
    require(retired.expired(), "Completed dispatches retained a replaced buffer");
}

void reused_dispatches(renderer::WorldCompute& compute, std::array<std::uint32_t, 384>& expected) {
    const auto allocations = compute.binding_allocations();
    require(allocations == 3, "Pending dispatches did not receive independent binding storage");
    for (unsigned frame = 0; frame < 4; ++frame) {
        std::array<std::uint32_t, 64> source{};
        for (unsigned index = 0; index < source.size(); ++index) {
            source[index] = 10000 + 100 * frame + index;
        }
        compute.allocate("reused_source", sizeof(source));
        compute.upload("reused_source", std::as_bytes(std::span(source)));
        compute.buffers["srcData"] = compute.buffers.at("reused_source");
        std::weak_ptr<renderer::Buffer> retired = compute.buffers.at("reused_source");
        for (unsigned dispatch = 0; dispatch < 3; ++dispatch) {
            const auto offset = frame + dispatch * 8;
            const auto destination = 160 + dispatch * 16;
            const auto count = 8 + dispatch;
            compute.run("_dataCopy_copyData", {1, 1, 1}, copy_values(offset, destination, count));
            std::copy_n(source.begin() + offset, count, expected.begin() + destination);
        }
        check_words(compute, std::span(expected).first(256));
        require(compute.binding_allocations() == allocations,
                "Steady dispatches allocated new uniform buffers or descriptor pools");
        compute.buffers.erase("reused_source");
        compute.buffers["srcData"] = compute.buffers.at("source_b");
        require(retired.expired(), "Cached binding slots retained a completed source buffer");
    }
    std::cout << "Three cached bindings served 12 further dispatches without new binding "
                 "allocations; retired source buffers were released each cycle\n";
}

void reject_attached_access(renderer::WorldCompute& compute) {
    bool rejected = false;
    try {
        static_cast<void>(compute.read("dstData", 0, 4));
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected, "An attached readback was not rejected before recording commands");
    rejected = false;
    try {
        compute.release_completed();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected, "An attached command buffer released its binding storage prematurely");
}

void attached_frame(renderer::WorldCompute& compute, std::array<std::uint32_t, 384>& expected) {
    auto& device = compute.device;
    auto pool = device.logical->createCommandPoolUnique(
        {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, device.family});
    const auto command =
        device.logical->allocateCommandBuffers({*pool, vk::CommandBufferLevel::ePrimary, 1})
            .front();
    command.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    compute.attach(command);
    reject_attached_access(compute);
    std::weak_ptr<renderer::Buffer> retired = compute.buffers.at("dstData");
    compute.run("_dataCopy_copyData", {1, 1, 1}, copy_values(8, 110, 8));
    compute.grow("dstData", expected.size() * 4);
    compute.detach();
    require(!retired.expired(), "Frame attachment released resources before submission");
    command.end();
    auto fence = device.logical->createFenceUnique({});
    const vk::CommandBufferSubmitInfo commands(command);
    vk::SubmitInfo2 submission;
    submission.setCommandBufferInfos(commands);
    device.queue.submit2(submission, *fence);
    require(device.logical->waitForFences(
                *fence, true, std::numeric_limits<std::uint64_t>::max()) == vk::Result::eSuccess,
            "Attached frame failed to complete");
    for (unsigned index = 0; index < 8; ++index) {
        expected[110 + index] = 1008 + index;
    }
    check_words(compute, expected);
    require(retired.expired(), "Completed frame retained obsolete destination resources");
}

void indirect_lifetime(renderer::WorldCompute& compute, std::array<std::uint32_t, 384>& expected) {
    const std::array<std::uint32_t, 3> arguments{1, 1, 1};
    compute.upload("indirect_arguments", std::as_bytes(std::span(arguments)));
    std::weak_ptr<renderer::Buffer> retired = compute.buffers.at("indirect_arguments");
    compute.run_indirect("_dataCopy_copyData", "indirect_arguments", copy_values(0, 130, 7));
    compute.buffers.erase("indirect_arguments");
    require(!retired.expired(), "Pending dispatch lost its indirect argument buffer");
    for (unsigned index = 0; index < 7; ++index) {
        expected[130 + index] = 1000 + index;
    }
    check_words(compute, expected);
    require(retired.expired(), "Completed indirect dispatch retained its argument buffer");
    compute.command().fillBuffer(*compute.buffers.at("dstData")->handle, 0, 16, 0xFFFFFFFFU);
    compute.cancel();
    check_words(compute, expected);
}

void invalid_range(renderer::WorldCompute& compute) {
    const std::array<std::uint32_t, 1> value{7};
    bool rejected = false;
    try {
        compute.upload("dstData", std::as_bytes(std::span(value)),
                       std::numeric_limits<vk::DeviceSize>::max() - 6);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "An overflowing aligned upload range was not rejected");
}

void image_binding_reuse(renderer::WorldCompute& compute) {
    renderer::test::verify_first_hit_data(compute);
    const auto allocations = compute.binding_allocations();
    const std::weak_ptr<renderer::Image> geometry = compute.chunks.geometry;
    const std::weak_ptr<renderer::Image> entities = compute.chunks.entities;
    compute.chunks = {};
    require(geometry.expired() && entities.expired(),
            "Cached descriptors retained completed world images");
    renderer::test::verify_first_hit_data(compute);
    require(compute.binding_allocations() == allocations,
            "Replacement images prevented reuse of completed binding storage");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(
            argc == 3 || argc == 4,
            "Usage: rendering_compute_test SHADER_DIRECTORY PROBE_DIRECTORY [--split|--packed]");
        auto window = engine::platform::window::Window::create({32, 32, "Compute bindings", false});
        require(window.has_value(), "GLFW window creation failed");
        renderer::Device device(*window, true, chunk_layout(argc, argv));
        renderer::BufferMap buffers;
        renderer::ChunkImages chunks;
        renderer::WorldCompute compute(device, argv[1], buffers, chunks);
        std::array<std::uint32_t, 384> expected{};
        pending_dispatches(compute, expected);
        reused_dispatches(compute, expected);
        attached_frame(compute, expected);
        indirect_lifetime(compute, expected);
        invalid_range(compute);
        require(compute.execution_counts().at("_dataCopy_copyData") == 17,
                "Original copy helper dispatch count changed");
        renderer::WorldCompute helpers(device, argv[2], buffers, chunks);
        renderer::test::verify_render_helpers(helpers);
        image_binding_reuse(compute);
        require(device.validation_errors.load() == 0, "Compute Vulkan validation failed");
        std::cout
            << "Independent bindings, completed-slot reuse, growth and frame lifetime passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
