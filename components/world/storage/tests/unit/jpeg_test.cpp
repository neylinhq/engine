#include "../../src/image/image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open JPEG test fixture: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), {}};
}

void verify_fixture(const std::filesystem::path& root, const std::string& name) {
    const auto encoded = read_bytes(root / (name + ".jpg"));
    const auto reference = read_bytes(root / (name + ".rgb"));
    const auto image = engine::world::storage::decode_jpeg(encoded);
    if (image.width != 23 || image.height != 19 || reference.size() != 12 + 23 * 19 * 3) {
        throw std::runtime_error("Wrong JPEG dimensions: " + name);
    }
    int maximum = 0;
    double total = 0;
    for (std::size_t index = 0; index < image.pixels.size(); ++index) {
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto actual = (image.pixels[index] >> (16 - channel * 8)) & 255;
            const auto expected = reference[12 + index * 3 + channel];
            const auto error = std::abs(static_cast<int>(actual) - expected);
            maximum = std::max(maximum, error);
            total += error;
        }
    }
    const auto mean = total / (image.pixels.size() * 3);
    std::cout << name << " max=" << maximum << " mean=" << mean << '\n';
    if (maximum > 3 || mean > 0.75) {
        throw std::runtime_error("JPEG reference mismatch: " + name);
    }
}

void reject_truncation(const std::filesystem::path& root) {
    const auto bytes = read_bytes(root / "rgb_2_1_0.jpg");
    for (std::size_t size = 0; size < bytes.size(); size += 17) {
        bool rejected = false;
        try {
            static_cast<void>(engine::world::storage::decode_jpeg(std::span(bytes).first(size)));
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected) {
            throw std::runtime_error("Truncated JPEG was accepted");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        return 2;
    }
    try {
        const auto source_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        const auto root =
            argc == 2 ? std::filesystem::path(argv[1]) : source_root / "tests/fixtures/jpeg";
        for (unsigned subsampling = 0; subsampling < 3; ++subsampling) {
            for (unsigned progressive = 0; progressive < 2; ++progressive) {
                for (unsigned optimized = 0; optimized < 2; ++optimized) {
                    verify_fixture(root, "rgb_" + std::to_string(subsampling) + "_" +
                                             std::to_string(progressive) + "_" +
                                             std::to_string(optimized));
                }
            }
        }
        for (const auto* name : {"l_0", "l_1", "cmyk_0", "cmyk_1", "restart_1", "restart_3"}) {
            verify_fixture(root, name);
        }
        reject_truncation(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
