#include "compute.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine::rendering::vulkan {

void WorldCompute::allocate(const std::string& name, vk::DeviceSize bytes, bool clear) {
    bytes = std::max<vk::DeviceSize>(4, bytes);
    if (bytes > device.properties.limits.maxStorageBufferRange || (bytes & 3U) != 0) {
        throw std::runtime_error("World buffer size exceeds aligned GPU storage range: " + name);
    }
    const auto previous = buffers.find(name);
    if (previous != buffers.end()) {
        retained_.push_back(previous->second);
    }
    const auto usage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc |
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndirectBuffer;
    buffers[name] = make_buffer(device, {bytes, usage, false});
    if (clear) {
        command().fillBuffer(*buffers.at(name)->handle, 0, bytes, 0);
        memory_barrier(command());
    }
}

void WorldCompute::grow(const std::string& name, vk::DeviceSize bytes) {
    const auto found = buffers.find(name);
    if (found == buffers.end()) {
        allocate(name, bytes);
        return;
    }
    if (found->second->size >= bytes) {
        return;
    }
    const auto previous = found->second;
    const auto maximum =
        vk::DeviceSize(device.properties.limits.maxStorageBufferRange) & ~vk::DeviceSize{3};
    if (bytes > maximum) {
        throw std::runtime_error("World buffer growth exceeds GPU storage range: " + name);
    }
    allocate(name, std::min(maximum, bytes * 2));
    command().copyBuffer(*previous->handle, *buffers.at(name)->handle,
                         vk::BufferCopy(0, 0, previous->size));
    memory_barrier(command());
}

void WorldCompute::upload(const std::string& name, std::span<const std::byte> bytes,
                          vk::DeviceSize offset) {
    if (bytes.empty()) {
        grow(name, 4);
        return;
    }
    const auto required = offset + bytes.size();
    if (required < offset) {
        throw std::runtime_error("World upload size overflow");
    }
    if (required > device.properties.limits.maxStorageBufferRange) {
        throw std::runtime_error("World upload exceeds GPU storage range: " + name);
    }
    grow(name, (required + 3U) & ~vk::DeviceSize{3});
    auto staging = make_buffer(device, {bytes.size(), vk::BufferUsageFlagBits::eTransferSrc, true});
    std::memcpy(staging->mapped, bytes.data(), bytes.size());
    retained_.push_back(staging);
    command().copyBuffer(*staging->handle, *buffers.at(name)->handle,
                         vk::BufferCopy(0, offset, bytes.size()));
    memory_barrier(command());
}

Bytes WorldCompute::read(const std::string& name, vk::DeviceSize offset, vk::DeviceSize count) {
    if (external_command_) {
        throw std::logic_error("Cannot read back world data while attached to a frame");
    }
    const auto& source = *buffers.at(name);
    if (offset > source.size || count > source.size - offset) {
        throw std::runtime_error("World readback exceeds buffer: " + name);
    }
    if (count == 0) {
        synchronize();
        return {};
    }
    const auto staging = make_buffer(device, {count, vk::BufferUsageFlagBits::eTransferDst, true});
    retained_.push_back(staging);
    command().copyBuffer(*source.handle, *staging->handle, vk::BufferCopy(offset, 0, count));
    const vk::MemoryBarrier2 barrier(
        vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead);
    command().pipelineBarrier2(vk::DependencyInfo({}, barrier));
    synchronize();
    Bytes result(static_cast<std::size_t>(count));
    std::memcpy(result.data(), staging->mapped, result.size());
    return result;
}

} // namespace engine::rendering::vulkan
