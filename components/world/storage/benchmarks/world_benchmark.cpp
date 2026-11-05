#include <engine/world/editing/editing.hpp>
#include <engine/world/storage/storage.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using Clock = std::chrono::steady_clock;

double milliseconds(Clock::time_point before) {
    return std::chrono::duration<double, std::milli>(Clock::now() - before).count();
}

engine::world::model::Vec3 first_occupied(const engine::world::model::Model& model) {
    const auto chunks = engine::world::model::chunk_size(model.size);
    for (std::size_t index = 0; index < model.chunks.size(); ++index) {
        if ((model.chunks[index] >> 30) == 0) {
            continue;
        }
        const auto x = static_cast<int>(index % chunks.x) * 16;
        const auto y = static_cast<int>(index / chunks.x % chunks.y) * 16;
        const auto z = static_cast<int>(index / chunks.x / chunks.y) * 16;
        for (int voxel = 0; voxel < 4096; ++voxel) {
            const engine::world::model::IVec3 position{x + voxel % 16, y + voxel / 16 % 16,
                                                       z + voxel / 256};
            if (engine::world::model::voxel_at(model, position) != 0) {
                return {position.x + 0.5, position.y + 0.5, position.z + 0.5};
            }
        }
    }
    throw std::runtime_error("Benchmark requires occupied voxel model");
}

void peak_memory() {
    std::ifstream input("/proc/self/status");
    std::string line;
    while (std::getline(input, line)) {
        if (line.starts_with("VmHWM:")) {
            std::cout << line << '\n';
        }
    }
}

void benchmark(const std::filesystem::path& path) {
    auto started = Clock::now();
    auto model = engine::world::storage::load_cvox(path);
    std::cout << "load_ms=" << milliseconds(started) << '\n';
    const auto center = first_occupied(model);
    engine::world::model::World world;
    started = Clock::now();
    world.replace(std::move(model), {4096, 512, 4096}, 4);
    std::cout << "generate_default_world_ms=" << milliseconds(started) << '\n';
    const auto view = world.view();
    std::cout << "chunk_words=" << view.chunks.size() << " block_words=" << view.blocks.size()
              << " voxel_words=" << view.voxels.size() << '\n';
    started = Clock::now();
    engine::world::editing::apply_brush(
        world, {engine::world::editing::Tool::sphere, center, 10, 1, true});
    std::cout << "radius10_erase_ms=" << milliseconds(started) << '\n';
    started = Clock::now();
    world.update_bounds(32768);
    std::cout << "background32768_cells_ms=" << milliseconds(started) << '\n';
    peak_memory();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: world_model_benchmark path/to/oasis.cvox\n";
        return 0;
    }
    try {
        benchmark(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
