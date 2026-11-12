#include <engine/world/editing/editing.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {

namespace model = engine::world::model;
namespace editing = engine::world::editing;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::size_t count(const model::World& world, std::uint16_t material) {
    std::size_t result = 0;
    const auto size = world.size();
    for (int z = 0; z < size.z; ++z) {
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                result += world.at({x, y, z}) == material;
            }
        }
    }
    return result;
}

model::World empty_world() {
    model::World world;
    world.replace(model::build_model({64, 64, 64}, {{}, {}, {}}, {}), {}, 1);
    return world;
}

void brushes() {
    auto world = empty_world();
    editing::apply_brush(world, {editing::Tool::sphere, {16, 16, 16}, 1, 1});
    require(count(world, 1) == 8, "Sphere voxel-center boundary is incorrect");
    editing::apply_brush(world, {editing::Tool::sphere, {16.5, 16.5, 16.5}, 1, 2});
    require(count(world, 2) == 1 && count(world, 1) == 7,
            "Sphere must exclude centers exactly on radius");
    editing::apply_brush(world, {editing::Tool::cube, {32, 32, 32}, 2, 1});
    require(count(world, 1) == 71, "Cube strict Chebyshev brush volume incorrect");
    editing::apply_brush(world, {editing::Tool::paint, {16, 16, 16}, 3, 2});
    require(count(world, 2) == 8 && count(world, 1) == 64,
            "Paint filled empty cells or skipped interior");
    editing::apply_brush(world, {editing::Tool::cube, {32, 32, 32}, 2, 1, true});
    require(count(world, 1) == 0 && count(world, 2) == 8, "Erase changed outside brush");
}

void filling() {
    auto world = empty_world();
    for (int x = 8; x < 24; ++x) {
        world.set({x, 8, 8}, 1);
    }
    world.set({24, 8, 8}, 2);
    world.set({24, 9, 8}, 1);
    world.commit_edits();
    editing::flood_fill(world, {8, 8, 8}, 2);
    require(count(world, 2) == 17 && count(world, 1) == 1,
            "Fill crossed a material barrier or diagonal adjacency");
    editing::flood_fill(world, {0, 0, 0}, 1);
    require(count(world, 1) == 1, "Fill may not flood empty space");
    editing::flood_fill(world, {8, 8, 8}, 0);
    require(count(world, 2) == 0 && count(world, 1) == 1,
            "Erase fill missed connected matching voxels");
}

void stamping() {
    auto world = empty_world();
    model::Material existing;
    existing.name = "red";
    existing.color_base = {1, 0, 0};
    world.set_material(1, existing);
    auto source = model::build_model({3, 2, 1}, {{}, existing}, {{{0, 0, 0}, 1}, {{2, 1, 0}, 1}});
    const auto index = world.add_model(std::move(source));
    require(world.materials().size() == 5, "Named material not shared or original dummy omitted");
    world.set({16, 16, 16}, 2);
    world.commit_edits();
    editing::stamp_model(world, world.models()[index], {15, 16, 16});
    require(world.at({15, 16, 16}) == 1 && world.at({17, 17, 16}) == 1 &&
                world.at({16, 16, 16}) == 2,
            "Stamp origin, palette remapping or empty-voxel preservation incorrect");
    editing::stamp_model(world, world.models()[index], {-2, 4, 4});
    require(world.at({0, 5, 4}) == 1, "Stamp clipping at world edge is incorrect");
    const auto revision = world.view().revision;
    editing::stamp_model(world, world.models()[index], {std::numeric_limits<int>::max(), 0, 0});
    editing::stamp_model(world, world.models()[index], {std::numeric_limits<int>::min(), 0, 0});
    editing::apply_brush(world, {editing::Tool::sphere, {1e100, 0, 0}, 1, 1});
    editing::apply_brush(world, {editing::Tool::cube, {-1e100, 0, 0}, 1, 1});
    require(world.view().revision == revision, "Far-outside brushes or stamps must be empty edits");
}

void reference_brush(std::vector<std::uint16_t>& cells, const editing::Brush& brush,
                     int size = 64) {
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto x = static_cast<int>(index % size);
        const auto y = static_cast<int>((index / size) % size);
        const auto z = static_cast<int>(index / (size * size));
        const double dx = x + 0.5 - brush.center.x;
        const double dy = y + 0.5 - brush.center.y;
        const double dz = z + 0.5 - brush.center.z;
        const bool inside =
            brush.tool == editing::Tool::cube
                ? std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) < brush.radius
                : dx * dx + dy * dy + dz * dz < brush.radius * brush.radius;
        auto& cell = cells[index];
        if (inside && (brush.tool != editing::Tool::paint || cell != 0)) {
            cell = brush.erase ? 0 : brush.material;
        }
    }
}

void parallel_brush_reference() {
    model::World world;
    world.replace(model::build_model({128, 128, 128}, {{}, {}, {}}, {}), {}, 1);
    std::vector<std::uint16_t> cells(128 * 128 * 128);
    for (const auto tool : {editing::Tool::sphere, editing::Tool::paint, editing::Tool::cube}) {
        const editing::Brush brush{tool, {64.5, 63.5, 65.5}, 63.0, 2, tool == editing::Tool::cube};
        reference_brush(cells, brush, 128);
        editing::apply_brush(world, brush);
        for (int index = 0; index < 128 * 128 * 128; ++index) {
            require(world.at({index % 128, (index / 128) % 128, index / 16384}) == cells[index],
                    "Parallel brush differs from independent dense reference");
        }
    }
}

void chunk_brush_reference() {
    auto world = empty_world();
    std::vector<std::uint16_t> cells(64 * 64 * 64);
    std::mt19937 random{1093};
    for (unsigned iteration = 0; iteration < 24; ++iteration) {
        const auto coordinate = [&] { return static_cast<double>(random() % 97) - 16.25; };
        const auto tool = static_cast<editing::Tool>(1 + iteration % 3);
        const editing::Brush brush{tool,
                                   {coordinate(), coordinate(), coordinate()},
                                   0.5 + static_cast<double>(random() % 65),
                                   static_cast<std::uint16_t>(1 + iteration % 2),
                                   iteration % 4 == 0};
        reference_brush(cells, brush);
        editing::apply_brush(world, brush);
        for (int index = 0; index < 64 * 64 * 64; ++index) {
            const model::Vec3i position{index % 64, (index / 64) % 64, index / 4096};
            require(world.at(position) == cells[index],
                    "Chunk brush differs from independent dense voxel reference");
        }
    }
}

} // namespace

int main() {
    try {
        brushes();
        filling();
        stamping();
        chunk_brush_reference();
        parallel_brush_reference();
        std::cout << "Original sphere/cube/paint/fill/stamp independent fixtures passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
