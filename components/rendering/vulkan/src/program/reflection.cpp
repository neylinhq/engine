#include "reflection.hpp"
#include "../gpu/device.hpp"

#include <algorithm>
#include <boost/json.hpp>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace engine::rendering::vulkan {
namespace {
namespace json = boost::json;

std::string text(const json::object& object, std::string_view name) {
    const auto* value = object.if_contains(name);
    return value && value->is_string() ? std::string(value->as_string()) : std::string{};
}

std::uint32_t integer(const json::object& object, std::string_view name) {
    const auto* value = object.if_contains(name);
    if (!value) {
        return 0;
    }
    return json::value_to<std::uint32_t>(*value);
}

Resource resource(const json::object& parameter) {
    const auto& binding = parameter.at("binding").as_object();
    const auto& type = parameter.at("type").as_object();
    Resource result{text(parameter, "name"), integer(binding, "index")};
    const auto shape = text(type, "baseShape");
    if (shape.starts_with("texture")) {
        result.type = text(type, "access") == "readWrite" ? vk::DescriptorType::eStorageImage
                                                          : vk::DescriptorType::eSampledImage;
    } else if (shape != "structuredBuffer" && shape != "byteAddressBuffer") {
        throw std::runtime_error("Unsupported reflected resource: " + result.name + " " + shape);
    }
    return result;
}

std::uint32_t uniform_end(const Uniform& uniform) {
    const auto end = std::uint64_t{uniform.offset} + uniform.size;
    if (end > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Reflected uniform range exceeds uint32 address space");
    }
    return static_cast<std::uint32_t>(end);
}

std::uint32_t uniform_allocation_size(std::uint32_t extent) {
    const auto aligned = (std::uint64_t{extent} + 15U) & ~std::uint64_t{15};
    if (aligned > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Reflected uniform alignment exceeds uint32 address space");
    }
    return std::max(16U, static_cast<std::uint32_t>(aligned));
}

void parameter(Reflection& result, const json::object& value) {
    const auto* raw_binding = value.if_contains("binding");
    if (!raw_binding) {
        return;
    }
    const auto& binding = raw_binding->as_object();
    if (text(binding, "kind") == "uniform") {
        Uniform uniform{integer(binding, "offset"), integer(binding, "size"), 0};
        uniform.stride = integer(value.at("type").as_object(), "uniformStride");
        result.uniform_size = std::max(result.uniform_size, uniform_end(uniform));
        result.uniforms.emplace(text(value, "name"), uniform);
    } else if (text(binding, "kind") == "descriptorTableSlot") {
        result.resources.push_back(resource(value));
    }
}
} // namespace

Reflection read_reflection(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Missing Slang reflection: " + path_text(path));
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    const auto root = json::parse(contents.str()).as_object();
    Reflection result;
    for (const auto& value : root.at("parameters").as_array()) {
        parameter(result, value.as_object());
    }
    const auto& entry = root.at("entryPoints").as_array().front().as_object();
    result.entry = text(entry, "name");
    const auto stage = text(entry, "stage");
    result.stage = stage == "vertex"     ? vk::ShaderStageFlagBits::eVertex
                   : stage == "fragment" ? vk::ShaderStageFlagBits::eFragment
                                         : vk::ShaderStageFlagBits::eCompute;
    const auto* bindings = entry.if_contains("bindings");
    if (bindings) {
        for (const auto& item : bindings->as_array()) {
            const auto& binding = item.as_object();
            const auto& location = binding.at("binding").as_object();
            if (location.if_contains("used") && integer(location, "used") == 0) {
                std::erase_if(result.resources, [&](const auto& resource) {
                    return resource.name == text(binding, "name");
                });
            }
        }
    }
    result.uniform_size = uniform_allocation_size(result.uniform_size);
    return result;
}

} // namespace engine::rendering::vulkan
