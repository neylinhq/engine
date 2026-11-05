#pragma once

#include "../image/image.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::world::storage::jpeg {

using Block = std::array<std::int32_t, 64>;
inline constexpr std::array<unsigned, 64> kZigzag{
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct Huffman {
    std::array<int, 17> minimum{};
    std::array<int, 17> maximum{};
    std::array<int, 17> first{};
    std::vector<std::uint8_t> symbols;
    bool present = false;
};

struct Component {
    unsigned id = 0;
    unsigned horizontal = 0;
    unsigned vertical = 0;
    unsigned quantization = 0;
    unsigned dc_table = 0;
    unsigned ac_table = 0;
    unsigned columns = 0;
    unsigned rows = 0;
    unsigned actual_columns = 0;
    unsigned actual_rows = 0;
    unsigned sample_width = 0;
    unsigned sample_height = 0;
    std::int32_t predictor = 0;
    std::array<int, 64> approximation{};
    std::array<std::uint16_t, 64> quantization_values{};
    bool quantization_captured = false;
    std::vector<Block> blocks;
    std::vector<std::uint8_t> samples;
};

struct Decoder {
    std::span<const std::uint8_t> bytes;
    std::size_t position = 0;
    unsigned width = 0;
    unsigned height = 0;
    unsigned max_horizontal = 1;
    unsigned max_vertical = 1;
    unsigned mcu_columns = 0;
    unsigned mcu_rows = 0;
    unsigned restart_interval = 0;
    int adobe_transform = -1;
    bool progressive = false;
    bool frame_seen = false;
    bool scan_seen = false;
    std::array<std::array<std::uint16_t, 64>, 4> quantization{};
    std::array<bool, 4> quantization_present{};
    std::array<Huffman, 4> dc_tables;
    std::array<Huffman, 4> ac_tables;
    std::vector<Component> components;
};

struct Scan {
    explicit Scan(Decoder& source) : decoder(source) {}
    Decoder& decoder;
    std::vector<unsigned> components;
    unsigned start = 0;
    unsigned end = 63;
    unsigned previous_bit = 0;
    unsigned bit = 0;
    unsigned remaining_bits = 0;
    std::uint32_t entropy_byte = 0;
    unsigned end_run = 0;
    unsigned restart_index = 0;
};

[[noreturn]] void fail(const char* reason);
[[nodiscard]] unsigned byte(Decoder& decoder);
[[nodiscard]] unsigned word(Decoder& decoder);
[[nodiscard]] unsigned marker(Decoder& decoder);
[[nodiscard]] unsigned bits(Scan& scan, unsigned count);
[[nodiscard]] int amplitude(Scan& scan, unsigned count);
[[nodiscard]] unsigned symbol(Scan& scan, const Huffman& table);
void read_quantization(Decoder& decoder, std::size_t end);
void read_huffman(Decoder& decoder, std::size_t end);
void read_frame(Decoder& decoder, std::size_t end, unsigned type);
void read_scan(Decoder& decoder, std::size_t end);
void decode_scan(Scan& scan);
void decode_block(Scan& scan, Component& component, Block& block);
void decode_progressive(Scan& scan, Component& component, Block& block);
[[nodiscard]] Image reconstruct(Decoder& decoder);
[[nodiscard]] Image decode(std::span<const std::uint8_t> bytes);

} // namespace engine::world::storage::jpeg
