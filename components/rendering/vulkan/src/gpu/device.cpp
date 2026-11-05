#include "device.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
VKAPI_ATTR VkBool32 VKAPI_CALL
validation_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                   const VkDebugUtilsMessengerCallbackDataEXT* message, void* context) {
    auto& device = *static_cast<Device*>(context);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        device.validation_errors.fetch_add(1, std::memory_order_relaxed);
    }
    std::fprintf(stderr, "Vulkan validation: %s\n", message->pMessage);
    return VK_FALSE;
}

std::vector<const char*> requested_layers(bool validation) {
    if (!validation) {
        return {};
    }
    const auto available = vk::enumerateInstanceLayerProperties();
    const bool found = std::ranges::any_of(available, [](const auto& layer) {
        return std::strcmp(layer.layerName.data(), "VK_LAYER_KHRONOS_validation") == 0;
    });
    if (!found) {
        throw std::runtime_error("Requested VK_LAYER_KHRONOS_validation is unavailable");
    }
    return {"VK_LAYER_KHRONOS_validation"};
}

std::optional<std::uint32_t> find_queue(vk::PhysicalDevice physical, vk::SurfaceKHR surface) {
    const auto queues = physical.getQueueFamilyProperties();
    const auto flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
    for (std::uint32_t index = 0; index < queues.size(); ++index) {
        if ((queues[index].queueFlags & flags) == flags &&
            physical.getSurfaceSupportKHR(index, surface)) {
            return index;
        }
    }
    return {};
}

bool supports_chunk_images(vk::PhysicalDevice physical) {
    const auto required =
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eStorageImage |
        vk::FormatFeatureFlagBits::eStorageImageAtomic | vk::FormatFeatureFlagBits::eTransferSrc |
        vk::FormatFeatureFlagBits::eTransferDst;
    const auto properties = physical.getFormatProperties(vk::Format::eR32Uint);
    return (properties.optimalTilingFeatures & required) == required;
}

bool supports_device(vk::PhysicalDevice physical) {
    if (physical.getProperties().apiVersion < VK_API_VERSION_1_3) {
        return false;
    }
    const auto extensions = physical.enumerateDeviceExtensionProperties();
    const bool swapchain = std::ranges::any_of(extensions, [](const auto& extension) {
        return std::strcmp(extension.extensionName.data(), VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
    const auto features =
        physical
            .getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                          vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();
    const auto& modern = features.get<vk::PhysicalDeviceVulkan13Features>();
    return swapchain && modern.synchronization2 && modern.dynamicRendering &&
           features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
           features.get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout &&
           features.get<vk::PhysicalDeviceVulkan12Features>().shaderFloat16 &&
           features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt16 &&
           features.get<vk::PhysicalDeviceFeatures2>().features.shaderStorageImageExtendedFormats &&
           supports_chunk_images(physical);
}
} // namespace

Surface::~Surface() {
    if (handle) {
        instance.destroySurfaceKHR(handle);
    }
}

Messenger::~Messenger() {
    if (handle && destroy) {
        destroy(instance, handle, nullptr);
    }
}

Device::Device(platform::window::Window& window, bool validation, ChunkLayout chunk_layout,
               const std::string& application_name) {
    create_instance(window, validation, application_name);
    if (validation) {
        create_messenger();
    }
    surface.instance = *instance;
    const auto native =
        window.create_surface(reinterpret_cast<std::uintptr_t>(static_cast<VkInstance>(*instance)));
    if (!native) {
        throw std::runtime_error(native.error());
    }
    surface.handle = vk::SurfaceKHR(std::bit_cast<VkSurfaceKHR>(*native));
    select_physical();
    packed_chunks = select_chunk_layout(physical, chunk_layout);
    create_logical();
}

Device::~Device() {
    if (logical) {
        try {
            logical->waitIdle();
        } catch (const vk::SystemError&) {
        }
    }
}

void Device::create_instance(platform::window::Window& window, bool validation,
                             const std::string& application_name) {
    auto extensions = window.instance_extensions();
    if (!extensions) {
        throw std::runtime_error(extensions.error());
    }
    if (validation) {
        extensions->push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    const auto layers = requested_layers(validation);
    const vk::ApplicationInfo application(application_name.c_str(), 1, "Engine", 1,
                                          VK_API_VERSION_1_3);
    vk::InstanceCreateInfo info;
    info.setPApplicationInfo(&application)
        .setPEnabledExtensionNames(*extensions)
        .setPEnabledLayerNames(layers);
    instance = vk::createInstanceUnique(info);
}

void Device::create_messenger() {
    messenger.instance = *instance;
    messenger.destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(*instance, "vkDestroyDebugUtilsMessengerEXT"));
    const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(*instance, "vkCreateDebugUtilsMessengerEXT"));
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = validation_message;
    info.pUserData = this;
    if (!create || !messenger.destroy || create(*instance, &info, nullptr, &messenger.handle)) {
        throw std::runtime_error("Cannot initialize Vulkan validation diagnostics");
    }
}

void Device::select_physical() {
    for (const auto candidate : instance->enumeratePhysicalDevices()) {
        if (!supports_device(candidate)) {
            continue;
        }
        const auto selected = find_queue(candidate, surface.handle);
        if (selected) {
            physical = candidate;
            family = *selected;
            break;
        }
    }
    if (!physical) {
        throw std::runtime_error("Vulkan 1.3 graphics/compute/present, scalar block layout and "
                                 "16-bit shader arithmetic, draw parameters and extended storage "
                                 "image formats, and sampled/storage/atomic R32_UINT images with "
                                 "transfer support are required");
    }
    properties = physical.getProperties();
    timestamp_bits = physical.getQueueFamilyProperties()[family].timestampValidBits;
}

namespace {
bool supports_memory_budget(vk::PhysicalDevice physical) {
    const auto supported = physical.enumerateDeviceExtensionProperties();
    return std::ranges::any_of(supported, [](const auto& extension) {
        return std::strcmp(extension.extensionName.data(), VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) ==
               0;
    });
}
} // namespace
void Device::create_logical() {
    const float priority = 1.0f;
    const vk::DeviceQueueCreateInfo queue_info({}, family, 1, &priority);
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    memory_budget = supports_memory_budget(physical);
    if (memory_budget) {
        extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }
    vk::PhysicalDeviceShaderImageAtomicInt64FeaturesEXT image_atomics;
    if (packed_chunks) {
        extensions.push_back(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME);
        image_atomics.setShaderImageInt64Atomics(true);
    }
    vk::PhysicalDeviceVulkan13Features features13;
    features13.setSynchronization2(true).setDynamicRendering(true);
    if (packed_chunks) {
        features13.setPNext(&image_atomics);
    }
    vk::PhysicalDeviceVulkan12Features features12;
    features12.setScalarBlockLayout(true).setShaderFloat16(true).setPNext(&features13);
    vk::PhysicalDeviceVulkan11Features features11;
    features11.setShaderDrawParameters(true).setPNext(&features12);
    vk::PhysicalDeviceFeatures features;
    features.setShaderStorageImageExtendedFormats(true).setShaderInt16(true).setShaderInt64(
        packed_chunks);
    features.setRobustBufferAccess(physical.getFeatures().robustBufferAccess);
    vk::DeviceCreateInfo info;
    info.setPNext(&features11)
        .setPEnabledFeatures(&features)
        .setQueueCreateInfos(queue_info)
        .setPEnabledExtensionNames(extensions);
    logical = physical.createDeviceUnique(info);
    queue = logical->getQueue(family, 0);
}

std::uint32_t Device::memory_type(std::uint32_t bits, vk::MemoryPropertyFlags required) const {
    const auto memory_properties = physical.getMemoryProperties();
    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((bits & (1u << index)) &&
            (memory_properties.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    throw std::runtime_error("Required Vulkan memory type is unavailable");
}

} // namespace engine::rendering::vulkan
