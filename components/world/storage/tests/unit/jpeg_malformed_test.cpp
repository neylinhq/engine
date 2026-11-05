#include "../../src/image/image.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open JPEG mutation fixture");
    }
    return {std::istreambuf_iterator<char>(input), {}};
}

std::size_t entropy_start(std::span<const std::uint8_t> bytes) {
    for (std::size_t index = 2; index + 4 < bytes.size(); ++index) {
        if (bytes[index] == 0xff && bytes[index + 1] == 0xda) {
            const auto length = (static_cast<unsigned>(bytes[index + 2]) << 8) | bytes[index + 3];
            if (index + 2 + length < bytes.size()) {
                return index + 2 + length;
            }
        }
    }
    throw std::runtime_error("Fixture has no entropy-coded scan");
}

std::vector<std::uint8_t> mutate(std::span<const std::uint8_t> original, std::size_t start,
                                 std::mt19937& random) {
    std::vector<std::uint8_t> result(original.begin(), original.end());
    const auto edits = 1 + random() % 4;
    for (unsigned index = 0; index < edits; ++index) {
        const auto position = start + random() % (result.size() - start);
        result[position] ^= static_cast<std::uint8_t>(1u << (random() % 8));
    }
    if (random() % 4 == 0) {
        result.resize(start + random() % (result.size() - start));
    }
    return result;
}

void exercise(const std::filesystem::path& file, std::mt19937& random) {
    const auto bytes = read_bytes(file);
    const auto start = entropy_start(bytes);
    unsigned rejected = 0;
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        const auto input = mutate(bytes, start, random);
        engine::world::storage::Image result;
        try {
            result = engine::world::storage::decode_jpeg(input);
        } catch (const std::runtime_error&) {
            ++rejected;
            continue;
        }
        if (result.width != 23 || result.height != 19 || result.pixels.size() != 23 * 19) {
            throw std::runtime_error("Entropy mutation changed the fixed frame dimensions");
        }
    }
    if (rejected == 0) {
        throw std::runtime_error("Malformed/truncated entropy was never rejected");
    }
    std::cout << file.filename().string() << ": 1000 bounded entropy mutations, " << rejected
              << " rejected\n";
}

} // namespace

int main() {
    try {
        const auto root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
            "tests/fixtures/jpeg";
        std::mt19937 random(0x6a706567U);
        for (const auto* name : {"rgb_2_0_0.jpg", "rgb_2_1_0.jpg", "restart_1.jpg"}) {
            exercise(root / name, random);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
