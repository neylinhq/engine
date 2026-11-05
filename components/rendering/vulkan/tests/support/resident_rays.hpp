#pragma once

#include "update_probe.hpp"

#include <optional>

namespace engine::rendering::vulkan::test {

using ResidentRay = std::array<float, 8>;
using ResidentResult = std::array<std::uint32_t, 8>;

class ResidentRays {
  public:
    ResidentRays(UpdateProbe& probe, const std::filesystem::path& shaders);
    void verify_static();
    void verify_entity(std::optional<world::model::Vec3> position, std::uint16_t material);

  private:
    UpdateProbe& probe_;
    WorldCompute compute_;
    [[nodiscard]] std::vector<ResidentResult> run(std::span<const ResidentRay> rays);
};

} // namespace engine::rendering::vulkan::test
