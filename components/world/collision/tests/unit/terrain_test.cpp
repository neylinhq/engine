#include <algorithm>
#include <cmath>
#include <cstdint>
#include <engine/foundation/geometry/vector.hpp>
#include <engine/motion/character/motor.hpp>
#include <engine/world/collision/placement.hpp>
#include <engine/world/collision/terrain.hpp>
#include <engine/world/editing/editing.hpp>
#include <engine/world/model/world.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace engine::world::collision {
namespace {

namespace character = motion::character;
constexpr double kVoxelMeters = 0.0625;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void advance(character::Motor& motor, const Terrain& field, character::Intent intent, int ticks) {
    for (int tick = 0; tick < ticks; ++tick) {
        motor.advance(field, intent, character::kTick).value();
        require(!field.penetration({motor.state().feet, character::kRadius, character::kHeight}),
                "Character ended a tick inside a voxel");
        intent.jump_pressed = false;
    }
}

void sweeps() {
    model::World world;
    world.replace(model::build_model({32, 32, 32}, {{}, {}}, {{{4, 0, 4}, 1}, {{4, 4, 4}, 1}}));
    const Terrain field{world.view(), kVoxelMeters};
    const character::Capsule above{{0.28125, 0.5, 0.28125}, 0.08, 0.25};
    const auto floor = field.sweep(above, {0, -0.5, 0});
    require(floor && floor->normal.y > 0.99 && std::abs(floor->point.y - 0.3125) < 1e-5 &&
                floor->fraction > 0.35 && floor->fraction < 0.375,
            "Downward capsule sweep did not report the voxel top");
    const character::SweepFilter below_upper_voxel{
        [](const character::SweepHit& hit, const void*) { return hit.point.y < 0.25; }};
    const auto lower = field.sweep(above, {0, -0.5, 0}, below_upper_voxel);
    require(lower && lower->normal.y > 0.99 && std::abs(lower->point.y - kVoxelMeters) < 1e-5,
            "A rejected contact prevented the sweep from finding a lower surface");
    const character::Capsule before{{0.1, 0.24, 0.28125}, 0.08, 0.25};
    const auto wall = field.sweep(before, {0.6, 0, 0});
    require(wall && wall->normal.x < -0.99 && wall->fraction > 0.09 && wall->fraction < 0.117,
            "Forward capsule sweep crossed a single voxel");
    const character::Capsule touching{{0.28125, 0.3125, 0.28125}, 0.08, 0.25};
    require(!field.penetration(touching) && !field.sweep(touching, {0.4, 0, 0}) &&
                !field.sweep(touching, {0, 0.4, 0}) && field.sweep(touching, {0, -0.4, 0}),
            "A touching surface blocked tangential motion or failed to block inward motion");
    const auto overlap = field.penetration({{0.28125, 0.28, 0.28125}, 0.08, 0.25});
    require(overlap && overlap->normal.y > 0.99 && std::abs(overlap->depth - 0.0325) < 1e-5,
            "SDK did not report the overlap direction and depth");
}

character::State traverse_doorway(int opening_voxels, double lateral_offset, bool run) {
    constexpr int kLeftWall = 8;
    model::World world;
    std::vector<model::Sample> samples;
    for (int z = 0; z < 64; ++z) {
        for (int x = 0; x < 32; ++x) {
            samples.push_back({{x, 0, z}, 1});
            if (z >= 24 && z < 40 && (x < kLeftWall || x >= kLeftWall + opening_voxels)) {
                for (int y = 1; y < 32; ++y) {
                    samples.push_back({{x, y, z}, 1});
                }
            }
        }
    }
    world.replace(model::build_model({32, 32, 64}, {{}, {}}, samples));
    const Terrain field{world.view(), kVoxelMeters};
    const auto start_x = (kLeftWall + opening_voxels * 0.5) * kVoxelMeters + lateral_offset;
    character::Motor motor({start_x, kVoxelMeters + 0.005, 0.5});
    const character::Intent forward{.horizontal = {0, 0, 1}, .run = run};
    advance(motor, field, forward, run ? 55 : 90);
    return motor.state();
}

void doorway_clearance() {
    const auto walking = traverse_doorway(16, 0.125, false);
    require(walking.feet.z > 3 && walking.grounded,
            "Walking slightly off center did not clear the one-meter doorway");
    const auto running = traverse_doorway(16, -0.125, true);
    require(running.feet.z > 3 && running.grounded,
            "Running slightly off center did not clear the one-meter doorway");
    const auto blocked = traverse_doorway(10, 0, false);
    require(blocked.feet.z < 1.5 && blocked.grounded,
            "Character walked through a doorway narrower than the capsule");
}

model::Model obstacle(int step_voxels, bool ceiling = false) {
    std::vector<model::Sample> samples;
    for (int z = 0; z < 64; ++z) {
        for (int x = 0; x < 32; ++x) {
            samples.push_back({{x, 0, z}, 1});
            if (z >= 24) {
                for (int y = 1; y <= step_voxels; ++y) {
                    samples.push_back({{x, y, z}, 1});
                }
            }
            if (ceiling) {
                samples.push_back({{x, 26, z}, 1});
            }
        }
    }
    return model::build_model({32, 48, 64}, {{}, {}}, samples);
}

void steps_and_ceiling() {
    model::World world;
    const character::Intent forward{.horizontal = {0, 0, 1}};
    const character::Vec3 start{1, kVoxelMeters + 0.02, 0.5};
    world.replace(obstacle(4));
    character::Motor stepping(start);
    advance(stepping, Terrain{world.view(), kVoxelMeters}, forward, 100);
    require(stepping.state().feet.z > 2 && stepping.state().grounded &&
                std::abs(stepping.state().feet.y - (5 * kVoxelMeters + 0.02)) < 0.003,
            "Character could not mount a 25 cm step");

    world.replace(obstacle(7));
    const Terrain stairs{world.view(), kVoxelMeters};
    const character::Intent run{.horizontal = {0, 0, 1}, .run = true};
    character::Motor fast(start);
    character::Motor slow(start);
    advance(fast, stairs, run, 52);
    for (int frame = 0; frame < 13; ++frame) {
        slow.advance(stairs, run, 4 * character::kTick).value();
    }
    require(fast.state().feet.z > 2 && fast.state().grounded &&
                std::abs(fast.state().feet.y - (8 * kVoxelMeters + 0.02)) < 0.003 &&
                length(fast.state().feet - slow.state().feet) < 1e-8,
            "Running up a 43.75 cm step depended on frame partition or failed to find support");

    world.replace(obstacle(8));
    character::Motor blocked(start);
    advance(blocked, Terrain{world.view(), kVoxelMeters}, forward, 100);
    require(blocked.state().feet.z < 1.5 && blocked.state().grounded,
            "Character mounted a step above its limit");

    world.replace(obstacle(0, true));
    character::Motor jumping(start);
    const Terrain roof{world.view(), kVoxelMeters};
    advance(jumping, roof, {.jump_pressed = true}, 1);
    require(jumping.state().velocity.y > 0, "A supported jump did not start");
    advance(jumping, roof, {}, 10);
    require(jumping.state().feet.y + character::kHeight <= 26 * kVoxelMeters &&
                jumping.state().velocity.y <= 0,
            "Jump passed through the ceiling");
}

model::Model staircase() {
    std::vector<model::Sample> samples;
    for (int z = 0; z < 80; ++z) {
        const auto height = 1 + std::clamp(z - 27, 0, 24);
        for (int x = 0; x < 32; ++x) {
            const auto top = x < 8 || x >= 24 ? height + 24 : height;
            for (int y = 0; y < top; ++y) {
                samples.push_back({{x, y, z}, static_cast<std::uint16_t>(1 + (x + y + z) % 2)});
            }
        }
    }
    return model::build_model({32, 64, 80}, {{}, {}, {}}, samples);
}

void stairs_and_rest() {
    model::World world;
    world.replace(staircase());
    const Terrain field{world.view(), kVoxelMeters};
    character::Motor climbing({1.125, kVoxelMeters + 0.02, 1});
    const character::Intent uphill{.horizontal = {0, 0, 1}};
    for (int tick = 0; tick < 48; ++tick) {
        const auto before = climbing.state();
        advance(climbing, field, uphill, 1);
        const auto after = climbing.state();
        require(after.grounded && after.feet.z > before.feet.z &&
                    after.eye.y >= before.eye.y - 1e-6,
                "Character lost support or moved backwards while climbing voxel stairs");
    }
    require(climbing.state().feet.z > 2.4 && climbing.state().feet.y > 0.8,
            "Walking could not climb a sequence of one-voxel steps");

    advance(climbing, field, {}, 160);
    const auto resting = climbing.state();
    require(resting.grounded && resting.velocity == character::Vec3{} && resting.feet.z < 3.1,
            "Character did not stop on the staircase");
    for (int frame = 0; frame < 90; ++frame) {
        climbing.advance(field, {}, frame % 2 == 0 ? 1.0 / 112 : 1.0 / 144).value();
        const auto state = climbing.state();
        require(state.grounded && state.feet == resting.feet && state.eye == resting.eye,
                "Stationary character or camera moved between stairs");
    }

    const character::Intent downhill{.horizontal = {0, 0, -1}};
    advance(climbing, field, downhill, 58);
    require(climbing.state().grounded && climbing.state().feet.z < 1.25 &&
                climbing.state().feet.y < 0.09,
            "Character could not walk back down the staircase");
}

void ledge_and_recovery() {
    model::World world;
    std::vector<model::Sample> samples;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 48; ++z) {
                samples.push_back({{x, y, z}, 1});
            }
        }
    }
    world.replace(model::build_model({48, 48, 48}, {{}, {}}, samples));
    const Terrain field{world.view(), kVoxelMeters};
    character::Motor supported({0.9, 1.29, 1.5});
    advance(supported, field, {}, 24);
    require(supported.state().grounded, "Capsule lost support with its center over the shelf");

    character::Motor overhang({1.32, 1.29, 1.5});
    advance(overhang, field, {}, 60);
    require(!overhang.state().grounded && overhang.state().feet.y < 1,
            "Capsule hung from a shelf outside its support area");

    character::Motor shallow({0.75, 1.24, 1.5});
    advance(shallow, field, {}, 2);
    require(shallow.state().grounded, "SDK contact could not resolve a shallow overlap");

    const character::Vec3 embedded{0.5, 0.25, 1.5};
    character::Motor deep(embedded);
    require(!deep.advance(field, {}, character::kTick) && deep.state().feet == embedded,
            "Deep overlap moved the character beyond the recovery limit");
}

void wall_slide() {
    model::World world;
    std::vector<model::Sample> samples;
    for (int z = 0; z < 96; ++z) {
        for (int x = 0; x < 40; ++x) {
            samples.push_back({{x, 0, z}, 1});
        }
        for (int y = 1; y < 32; ++y) {
            samples.push_back({{32, y, z}, 1});
        }
    }
    world.replace(model::build_model({48, 48, 96}, {{}, {}}, samples));
    const Terrain field{world.view(), kVoxelMeters};
    const character::Intent into_wall{.horizontal = {1, 0, 1}};
    character::Motor motor({1.2, kVoxelMeters + 0.02, 0.75});
    advance(motor, field, into_wall, 110);
    require(motor.state().feet.x + character::kRadius <= 2 && motor.state().feet.z > 3 &&
                motor.state().velocity.z > 2 && motor.state().grounded,
            "Diagonal input caught on the wall instead of sliding across voxel seams");

    advance(motor, field, {}, 40);
    const auto eye = motor.state().eye;
    for (int frame = 0; frame < 30; ++frame) {
        motor.advance(field, {}, 1.0 / 112).value();
        require(motor.state().eye == eye, "An idle character marked the camera as moving");
    }
}

void sweep_along_wall_seam() {
    model::World world;
    world.replace(model::build_model(
        {48, 48, 48}, {{}, {}},
        {{{32, 7, 23}, 1}, {{32, 7, 24}, 1}, {{32, 8, 23}, 1}, {{32, 8, 24}, 1}}));
    const Terrain field{world.view(), kVoxelMeters};
    const character::Capsule body{
        {2 - character::kRadius - 0.0037, 0.0945, 1.498}, character::kRadius, character::kHeight};
    require(!field.sweep(body, {0, 0.057, 0.027}),
            "A wall seam blocked a rising sweep that stayed clear of the wall");
    require(field.sweep(body, {0.02, 0.057, 0.027}).has_value(),
            "An inward sweep missed the wall at a voxel seam");
}

void jump_at_wall() {
    model::World world;
    world.replace(obstacle(40));
    const Terrain field{world.view(), kVoxelMeters};
    const character::Vec3 forward{0, 0, 1};
    character::Motor motor({1, kVoxelMeters + 0.02, 0.5});
    advance(motor, field, {.horizontal = forward}, 100);
    require(motor.state().grounded && field.sweep(motor.capsule(), forward * 0.01),
            "Jump test failed to reach the wall");
    for (const auto direction : {character::Vec3{}, forward}) {
        auto jumping = motor;
        advance(jumping, field, {.horizontal = direction, .jump_pressed = true}, 16);
        require(!jumping.state().grounded && jumping.state().feet.y > motor.state().feet.y + 0.5,
                "The first Space press at the wall did not start a jump");
    }
}

void guarded_placement() {
    model::World world;
    world.replace(obstacle(0));
    const auto paint = world.add_material({});
    const auto stamp =
        world.add_model(model::build_model({9, 1, 1}, {{}, {}}, {{{0, 0, 0}, 1}, {{8, 0, 0}, 1}}));
    character::Motor motor({1, kVoxelMeters + 0.02, 1});
    advance(motor, Terrain{world.view(), kVoxelMeters}, {}, 1);
    const auto floor_material = world.at({16, 0, 16});
    const auto revision = world.view().geometry_revision;
    const CapsulePlacement placement{motor.capsule(), kVoxelMeters};
    const auto guard = placement.guard();

    for (const auto tool : {editing::Tool::cube, editing::Tool::sphere}) {
        require(!editing::apply_brush(world, {tool, {16, 1, 16}, 6, paint}, guard),
                "Brush occupied part of the character's capsule");
    }
    require(!editing::stamp_model(world, world.models()[stamp], {8, 2, 16}, guard),
            "Model stamp occupied part of the character's capsule");
    world.fill_chunk({1, 0, 1}, paint);
    require(!world.commit_edits(guard), "A uniform chunk filled the character's capsule");
    world.commit_edits();
    require(world.view().geometry_revision == revision && world.at({16, 0, 16}) == floor_material &&
                world.at({16, 1, 16}) == 0 && world.at({8, 2, 16}) == 0,
            "Rejected placement left changes in the world or the pending batch");

    constexpr model::Vec3i kCornerVoxel{11, 8, 11};
    require(
        editing::apply_brush(world, {editing::Tool::cube, {11.5, 8.5, 11.5}, 0.5, paint}, guard) &&
            world.at(kCornerVoxel) == paint,
        "A voxel in the corner of the capsule bounds blocked placement");
    require(
        editing::apply_brush(world, {editing::Tool::paint, {16.5, 0.5, 16.5}, 0.5, paint}, guard) &&
            world.at({16, 0, 16}) == paint,
        "The placement guard blocked repainting");
    require(editing::apply_brush(world, {editing::Tool::cube, {16.5, 0.5, 16.5}, 0.5, 0, true},
                                 guard) &&
                world.at({16, 0, 16}) == 0,
            "The placement guard blocked erasing");
    advance(motor, Terrain{world.view(), kVoxelMeters}, {}, 1);

    const CapsulePlacement higher{
        {{1, 2 * kVoxelMeters, 1}, character::kRadius, character::kHeight}, kVoxelMeters};
    require(editing::apply_brush(world, {editing::Tool::cube, {16.5, 1.5, 16.5}, 0.5, paint},
                                 higher.guard()) &&
                world.at({16, 1, 16}) == paint,
            "Building directly below the feet was blocked at tangency");
}

void current_geometry() {
    model::World world;
    std::vector<model::Sample> samples;
    for (int z = 0; z < 64; ++z) {
        for (int x = 0; x < 64; ++x) {
            samples.push_back({{x, 0, z}, 1});
        }
    }
    world.replace(model::build_model({64, 64, 64}, {{}, {}}, samples));
    character::Motor motor({2, kVoxelMeters + 0.005, 2});
    motor.advance(Terrain{world.view(), 0.0625}, {}, character::kTick).value();
    require(motor.state().grounded, "Motor and SDK terrain queries failed to agree on support");
    for (int z = 1; z <= 2; ++z) {
        for (int x = 1; x <= 2; ++x) {
            world.fill_chunk({x, 0, z}, 0);
        }
    }
    world.commit_edits();
    motor.advance(Terrain{world.view(), 0.0625}, {}, 0.1).value();
    require(!motor.state().grounded && motor.state().velocity.y < 0,
            "Committed removal did not remove ground support");
    world.fill_chunk({1, 0, 1}, 1);
    world.commit_edits();
    const Terrain updated{world.view(), 0.0625};
    require(updated.penetration({{1.5, 0.2, 1.5}, 0.15, 0.5}).has_value(),
            "Uniform occupied chunk was not detected after editing");
}

void verify() {
    sweeps();
    doorway_clearance();
    steps_and_ceiling();
    stairs_and_rest();
    ledge_and_recovery();
    wall_slide();
    sweep_along_wall_seam();
    jump_at_wall();
    guarded_placement();
    current_geometry();
}

} // namespace
} // namespace engine::world::collision

int main() {
    try {
        engine::world::collision::verify();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
