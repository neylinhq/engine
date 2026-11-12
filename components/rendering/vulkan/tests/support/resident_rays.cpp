#include "resident_rays.hpp"

#include "../../src/frame/values.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>

namespace engine::rendering::vulkan::test {
namespace {

using Position = world::model::Vec3;

ResidentRay ray(Position origin, Position direction) {
    direction = foundation::geometry::normalized(direction);
    return {static_cast<float>(origin.x),    static_cast<float>(origin.y),
            static_cast<float>(origin.z),    0,
            static_cast<float>(direction.x), static_cast<float>(direction.y),
            static_cast<float>(direction.z), 0};
}

std::vector<ResidentRay> static_rays(world::model::Vec3i size) {
    std::mt19937 random(0xAADF2026U);
    std::uniform_real_distribution<double> unit(0.01, 0.99);
    std::uniform_real_distribution<double> direction(-1, 1);
    std::vector<ResidentRay> rays;
    for (unsigned index = 0; index < 1000; ++index) {
        rays.push_back(ray({unit(random) * size.x, unit(random) * size.y, unit(random) * size.z},
                           {direction(random), direction(random), direction(random)}));
    }
    for (const Position target :
         {Position{17.5, 21.5, 19.5}, {81.5, 21.5, 83.5}, {29.5, 25.5, 29.5}, {70.5, 20.5, 70.5}}) {
        rays.push_back(ray({0.5, target.y, target.z}, {1, 0, 0}));
        rays.push_back(ray({size.x - 0.5, target.y, target.z}, {-1, 0, 0}));
        rays.push_back(ray({target.x, 0.5, target.z}, {0, 1, 0}));
        rays.push_back(ray({target.x, size.y - 0.5, target.z}, {0, -1, 0}));
        rays.push_back(ray({target.x, target.y, 0.5}, {0, 0, 1}));
        rays.push_back(ray({target.x, target.y, size.z - 0.5}, {0, 0, -1}));
    }
    return rays;
}

Position rotate_y(Position point, double angle) {
    return {std::cos(angle) * point.x + std::sin(angle) * point.z, point.y,
            -std::sin(angle) * point.x + std::cos(angle) * point.z};
}

std::optional<double> entity_distance(const ResidentRay& ray, std::optional<Position> position) {
    if (!position) {
        return {};
    }
    const auto origin = rotate_y(Position{ray[0], ray[1], ray[2]} - *position, -2);
    const auto direction =
        rotate_y(foundation::geometry::normalized(Position{ray[4], ray[5], ray[6]}), -2);
    double near = 0;
    double far = 1e30;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-12) {
            if (origin[axis] < 0 || origin[axis] >= 4) {
                return {};
            }
            continue;
        }
        auto first = -origin[axis] / direction[axis];
        auto last = (4 - origin[axis]) / direction[axis];
        if (first > last) {
            std::swap(first, last);
        }
        near = std::max(near, first);
        far = std::min(far, last);
    }
    return near <= far ? std::optional<double>(near) : std::nullopt;
}

std::vector<ResidentRay> entity_rays() {
    std::vector<ResidentRay> rays;
    for (const Position position : {Position{30, 40, 40}, {105, 40, 100}}) {
        for (const auto fraction : {0.01, 0.25, 0.5, 0.75, 0.99}) {
            const auto target = position + rotate_y({fraction * 4, 2, 2}, 2);
            const Position direction{0.013, -1, 0.009};
            rays.push_back(ray(target - direction * 18, direction));
        }
    }
    rays.push_back(ray({120, 60, 120}, {0, 1, 0}));
    return rays;
}

} // namespace

ResidentRays::ResidentRays(UpdateProbe& probe, const std::filesystem::path& shaders)
    : probe_(probe), compute_(probe.device, shaders, probe.buffers, probe.chunks) {}

std::vector<ResidentResult> ResidentRays::run(std::span<const ResidentRay> rays) {
    probe_.compute.synchronize();
    const auto world = probe_.world.view();
    compute_.upload("voxelTypeData", std::as_bytes(world.materials));
    compute_.upload("test_rays", std::as_bytes(rays));
    compute_.allocate("test_results", rays.size() * sizeof(ResidentResult), false);
    UniformValues values;
    const std::array<float, 3> lower{0.1F, 0.1F, 0.1F};
    const std::array<float, 3> upper{world.size.x - 0.1F, world.size.y - 0.1F, world.size.z - 0.1F};
    set_value(values, "boundingBoxMin", lower);
    set_value(values, "boundingBoxMax", upper);
    set_value(values, "ray_count", static_cast<std::uint32_t>(rays.size()));
    compute_.run("trace_probe", {static_cast<unsigned>((rays.size() + 63) / 64), 1, 1}, values);
    const auto bytes = compute_.read("test_results", 0, rays.size() * sizeof(ResidentResult));
    std::vector<ResidentResult> result(rays.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

void ResidentRays::verify_static() {
    const auto rays = static_rays(probe_.world.size());
    const auto results = run(rays);
    unsigned hits = 0;
    for (std::size_t index = 0; index < rays.size(); ++index) {
        const auto& input = rays[index];
        const auto expected = probe_.world.trace({input[0], input[1], input[2]},
                                                 {input[4], input[5], input[6]}, 2000);
        const auto& actual = results[index];
        const auto length = expected.distance == 0 ? 100000000.0 : expected.distance;
        require_update(expected.hit == (actual[0] != 0),
                       "Resident GPU ray missed or invented edited geometry");
        require_update(!expected.hit || (actual[1] == expected.material &&
                                         std::abs(std::bit_cast<float>(actual[2]) - length) < 0.02),
                       "Resident GPU ray returned wrong edited material or hit distance");
        hits += expected.hit;
    }
    require_update(hits >= 10, "Resident ray corpus did not exercise enough edited geometry");
    std::cout << rays.size() << " production rays traversed resident GPU edits, " << hits
              << " hits matched independent CPU traversal\n";
}

void ResidentRays::verify_entity(std::optional<Position> position, std::uint16_t material) {
    const auto rays = entity_rays();
    const auto results = run(rays);
    for (std::size_t index = 0; index < rays.size(); ++index) {
        const auto expected = entity_distance(rays[index], position);
        const auto& actual = results[index];
        require_update(expected.has_value() == (actual[0] != 0),
                       "Resident GPU entity ray disagrees with rotated-box oracle");
        require_update(!expected || (actual[1] == material && actual[3] == 0 &&
                                     std::abs(std::bit_cast<float>(actual[2]) - *expected) < 0.02),
                       "Resident GPU entity ray returned wrong material, instance, or distance");
    }
    std::cout << rays.size() << " production rays traversed resident GPU entity lists\n";
}

} // namespace engine::rendering::vulkan::test
