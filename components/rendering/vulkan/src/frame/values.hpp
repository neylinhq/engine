#ifndef ENGINE_RENDERING_VULKAN_SRC_FRAME_VALUES_HPP
#define ENGINE_RENDERING_VULKAN_SRC_FRAME_VALUES_HPP

#include "../state.hpp"
#include <cstring>
#include <type_traits>

namespace engine::rendering::vulkan {

template <typename Value>
void set_value(UniformValues& values, const std::string& name, const Value& value) {
    static_assert(std::is_trivially_copyable_v<Value>);
    auto& bytes = values[name];
    bytes.resize(sizeof(Value));
    std::memcpy(bytes.data(), &value, sizeof(Value));
}

inline void set_boolean(UniformValues& values, const std::string& name, bool value) {
    set_value(values, name, std::uint32_t(value));
}

inline std::uint32_t random_value(std::uint32_t value) {
    const auto state = value * 747796405u + 2891336453u;
    const auto word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

inline float random_unit(std::uint32_t value) {
    return static_cast<float>(random_value(value) >> 8u) / 16777216.0f;
}

} // namespace engine::rendering::vulkan
#endif
