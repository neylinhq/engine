#include "../state.hpp"
#include <stdexcept>
namespace engine::rendering::vulkan {
DeviceInfo Renderer::device_info() const {
    const auto properties =
        impl_->device.physical
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties,
                            vk::PhysicalDeviceDriverProperties>();
    const auto& base = properties.get<vk::PhysicalDeviceProperties2>().properties;
    const auto& driver = properties.get<vk::PhysicalDeviceDriverProperties>();
    DeviceInfo result;
    result.name = base.deviceName.data();
    result.driver_name = driver.driverName.data();
    result.driver_info = driver.driverInfo.data();
    result.vendor_id = base.vendorID;
    result.device_id = base.deviceID;
    result.driver_version = base.driverVersion;
    result.api_version = base.apiVersion;
    result.queue_family = impl_->device.family;
    result.timestamp_bits = impl_->device.timestamp_bits;
    result.timestamp_period_nanoseconds = base.limits.timestampPeriod;
    constexpr std::string_view hex = "0123456789abcdef";
    for (const auto byte : properties.get<vk::PhysicalDeviceIDProperties>().deviceUUID) {
        result.uuid += hex[byte >> 4];
        result.uuid += hex[byte & 15];
    }
    return result;
}
MemoryStatistics Renderer::memory_statistics() const {
    vk::PhysicalDeviceMemoryBudgetPropertiesEXT budget;
    vk::PhysicalDeviceMemoryProperties2 properties;
    if (impl_->device.memory_budget) {
        properties.pNext = &budget;
    }
    impl_->device.physical.getMemoryProperties2(&properties);
    MemoryStatistics result;
    result.renderer_allocated_bytes = impl_->allocated_bytes();
    result.budget_supported = impl_->device.memory_budget;
    for (std::uint32_t index = 0; index < properties.memoryProperties.memoryHeapCount; ++index) {
        const auto& heap = properties.memoryProperties.memoryHeaps[index];
        result.heaps.push_back({heap.size, budget.heapBudget[index], budget.heapUsage[index],
                                bool(heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal)});
    }
    return result;
}
std::expected<void, std::string> Renderer::finish() {
    try {
        if (impl_->acquired) {
            throw std::logic_error("Cannot finish an acquired frame before draw");
        }
        impl_->complete_frame();
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}
} // namespace engine::rendering::vulkan
