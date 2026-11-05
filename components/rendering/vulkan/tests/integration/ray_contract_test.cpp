#include "../support/chunks/layout.hpp"
#include "../support/trace_probe.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>

namespace {
namespace model = engine::world::model;
namespace geometry = engine::foundation::geometry;
using engine::rendering::vulkan::test::Ray;
using engine::rendering::vulkan::test::RayRecord;
using engine::rendering::vulkan::test::TraceProbe;

Ray ray(model::Vec3 origin, model::Vec3 direction) {
    direction = geometry::normalized(direction);
    return {static_cast<float>(origin.x),    static_cast<float>(origin.y),
            static_cast<float>(origin.z),    0,
            static_cast<float>(direction.x), static_cast<float>(direction.y),
            static_cast<float>(direction.z), 0};
}

void append_box(std::vector<model::Sample>& samples, model::IVec3 origin, model::IVec3 size,
                std::uint16_t material) {
    const auto count = size.x * size.y * size.z;
    for (int index = 0; index < count; ++index) {
        samples.push_back({{origin.x + index % size.x, origin.y + (index / size.x) % size.y,
                            origin.z + index / (size.x * size.y)},
                           material});
    }
}

model::World static_world() {
    std::vector<model::Sample> samples;
    append_box(samples, {0, 1, 0}, {64, 1, 64}, 1);
    append_box(samples, {16, 16, 16}, {32, 16, 16}, 2);
    append_box(samples, {40, 12, 36}, {4, 4, 4}, 3);
    for (int index = 0; index < 200; ++index) {
        samples.push_back(
            {{(index * 13 + 3) % 64, (index * 29 + 3) % 64, (index * 7 + 5) % 64}, 4});
    }
    std::vector<model::Material> materials(5);
    model::World world;
    world.replace(model::build_model({64, 64, 64}, materials, samples), {}, 1);
    return world;
}

std::vector<Ray> static_rays() {
    std::mt19937 random(0x6e616164U);
    std::uniform_real_distribution<double> position(0.25, 63.25);
    std::uniform_real_distribution<double> direction(-1, 1);
    std::vector<Ray> rays;
    for (unsigned index = 0; index < 1000; ++index) {
        const model::Vec3 origin{position(random), position(random), position(random)};
        const model::Vec3 toward{direction(random), direction(random), direction(random)};
        rays.push_back(ray(origin, toward));
    }
    rays.push_back(ray({20.5, 20.5, 10.5}, {0, 0, 1}));
    rays.push_back(ray({37.5, 20.5, 10.5}, {0, 0, 1}));
    return rays;
}

void compare_static(const model::World& world, std::span<const Ray> rays,
                    std::span<const RayRecord> records) {
    unsigned hits = 0;
    for (std::size_t index = 0; index < rays.size(); ++index) {
        const auto& input = rays[index];
        const auto expected =
            world.trace({input[0], input[1], input[2]}, {input[4], input[5], input[6]}, 2000);
        const auto& actual = records[index];
        const auto distance = std::bit_cast<float>(actual[2]);
        // The original GPU routine returns its far sentinel when the ray starts
        // in an occupied cell; the CPU picker reports a zero-distance hit.
        const auto expected_distance = expected.distance == 0 ? 100000000.0 : expected.distance;
        const bool mismatch = expected.hit != (actual[0] != 0) ||
                              (expected.hit && (expected.material != actual[1] ||
                                                std::abs(expected_distance - distance) > 0.02));
        if (mismatch) {
            std::cerr << "Ray " << index << " CPU " << expected.hit << '/' << expected.material
                      << '/' << expected.distance << " GPU " << actual[0] << '/' << actual[1] << '/'
                      << distance << " origin " << input[0] << ',' << input[1] << ',' << input[2]
                      << " direction " << input[4] << ',' << input[5] << ',' << input[6] << '\n';
            throw std::runtime_error("CPU and production Slang traversal disagree");
        }
        hits += expected.hit;
    }
    if (hits < 100 || hits == rays.size()) {
        throw std::runtime_error("Ray corpus does not exercise both hits and misses");
    }
    std::cout << rays.size() << " static GPU rays matched CPU, " << hits << " hits\n";
}

void static_contract(TraceProbe& probe) {
    auto world = static_world();
    const auto rays = static_rays();
    compare_static(world, rays, probe.run(world, rays));
    world.set({20, 20, 16}, 3);
    world.commit_edits();
    compare_static(world, rays, probe.run(world, rays));
    world.set({20, 20, 16}, 0);
    world.commit_edits();
    compare_static(world, rays, probe.run(world, rays));
}

model::Vec3 rotate_y(model::Vec3 point, double angle) {
    return {std::cos(angle) * point.x + std::sin(angle) * point.z, point.y,
            -std::sin(angle) * point.x + std::cos(angle) * point.z};
}

struct EntityOracle {
    model::Vec3 position;
    model::IVec3 size;
    std::uint16_t material = 0;
};

std::optional<double> box_hit(model::Vec3 origin, model::Vec3 direction, model::IVec3 size) {
    const model::Vec3 maximum{static_cast<double>(size.x), static_cast<double>(size.y),
                              static_cast<double>(size.z)};
    double near = 0;
    double far = 1e30;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-12) {
            if (origin[axis] < 0 || origin[axis] >= maximum[axis]) {
                return {};
            }
            continue;
        }
        auto first = -origin[axis] / direction[axis];
        auto last = (maximum[axis] - origin[axis]) / direction[axis];
        if (first > last) {
            std::swap(first, last);
        }
        near = std::max(near, first);
        far = std::min(far, last);
    }
    return near <= far ? std::optional<double>(near) : std::nullopt;
}

void compare_entities(std::span<const Ray> rays, std::span<const RayRecord> records,
                      std::span<const EntityOracle> entities) {
    for (std::size_t index = 0; index < rays.size(); ++index) {
        const auto& input = rays[index];
        const model::Vec3 origin{input[0], input[1], input[2]};
        const auto direction = geometry::normalized(model::Vec3{input[4], input[5], input[6]});
        double closest = 1e30;
        std::size_t selected = entities.size();
        for (std::size_t entity = 0; entity < entities.size(); ++entity) {
            const auto distance = box_hit(rotate_y(origin - entities[entity].position, -2),
                                          rotate_y(direction, -2), entities[entity].size);
            if (distance && *distance < closest) {
                closest = *distance;
                selected = entity;
            }
        }
        const auto& actual = records[index];
        const auto hit = selected != entities.size();
        if (hit != (actual[0] != 0) ||
            (hit && (actual[1] != entities[selected].material || actual[3] != selected ||
                     std::abs(std::bit_cast<float>(actual[2]) - closest) > 0.02))) {
            std::cerr << "Entity ray " << index << " expected " << hit << '/' << selected << '/'
                      << closest << " actual " << actual[0] << '/' << actual[1] << '/' << actual[3]
                      << '/' << std::bit_cast<float>(actual[2]) << '\n';
            throw std::runtime_error(
                "Rotated entity GPU traversal differs from independent slab oracle");
        }
    }
    std::cout << rays.size() << " entity GPU rays matched independent rotated-box oracle\n";
}

model::Model entity_model(model::IVec3 size, std::string name) {
    std::vector<model::Sample> samples;
    append_box(samples, {}, size, 1);
    std::vector<model::Material> materials(2);
    materials[1].name = std::move(name);
    return model::build_model(size, materials, samples);
}

std::vector<Ray> entity_rays(std::span<const EntityOracle> entities) {
    std::vector<Ray> rays;
    for (const auto& entity : entities) {
        for (const auto fraction : {0.01, 0.25, 0.5, 0.75, 0.99}) {
            const model::Vec3 local{fraction * entity.size.x, entity.size.y * 0.5,
                                    entity.size.z * 0.5};
            const auto target = entity.position + rotate_y(local, 2);
            const model::Vec3 direction{0.013, -1, 0.009};
            rays.push_back(ray(target - direction * 20, direction));
        }
    }
    rays.push_back(ray({240, 100, 240}, {1, 0, 0}));
    rays.push_back(ray({5, 100, 5}, {0, 1, 0}));
    return rays;
}

void entity_contract(TraceProbe& probe) {
    model::World world;
    world.replace(model::build_model({256, 128, 256}, std::vector<model::Material>(1), {}), {}, 1);
    std::array<EntityOracle, 2> entities{
        {{{100, 40, 120}, {128, 3, 2}, 1}, {{220, 60, 30}, {5, 5, 5}, 2}}};
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const auto model =
            world.add_model(entity_model(entities[index].size, "entity" + std::to_string(index)));
        const auto type = world.create_entity_type(model);
        const auto named = std::ranges::find(world.materials(), "entity" + std::to_string(index),
                                             &model::Material::name);
        if (named == world.materials().end()) {
            throw std::runtime_error("Entity material was not registered globally");
        }
        entities[index].material = static_cast<std::uint16_t>(named - world.materials().begin());
        static_cast<void>(world.spawn_entity(type, entities[index].position));
    }
    world.update_entities(0, 0, 126);
    auto rays = entity_rays(entities);
    compare_entities(rays, probe.run(world, rays), entities);
    entities[0].position = {110, 45, 130};
    world.set_entity_position(0, entities[0].position);
    world.update_entities(0, 0, 125);
    rays = entity_rays(entities);
    compare_entities(rays, probe.run(world, rays), entities);
    world.clear_entities();
    compare_entities(rays, probe.run(world, rays), {});
}

} // namespace

int main(int count, char** arguments) {
    try {
        if (count != 3 && count != 4) {
            throw std::runtime_error(
                "Usage: rendering_ray_test TRACE_PROBE.spv TRACE_PROBE.json [--packed|--split]");
        }
        const auto layout = count == 4
                                ? engine::rendering::vulkan::test::parse_chunk_layout(arguments[3])
                                : engine::rendering::vulkan::ChunkLayout::automatic;
        TraceProbe probe(arguments[1], arguments[2], layout);
        static_contract(probe);
        entity_contract(probe);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
