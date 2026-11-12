#include "zip.hpp"

#include <algorithm>
#include <array>

namespace engine::world::storage {
namespace {

class BitWriter {
  public:
    void bits(unsigned value, unsigned count) {
        pending_ |= value << used_;
        used_ += count;
        while (used_ >= 8) {
            bytes_.push_back(static_cast<std::byte>(pending_ & 255U));
            pending_ >>= 8;
            used_ -= 8;
        }
    }

    void huffman(unsigned value, unsigned count) {
        unsigned reversed = 0;
        for (unsigned index = 0; index < count; ++index) {
            reversed = (reversed << 1) | ((value >> index) & 1U);
        }
        bits(reversed, count);
    }

    std::vector<std::byte> finish() {
        if (used_ != 0) {
            bytes_.push_back(static_cast<std::byte>(pending_));
        }
        return std::move(bytes_);
    }

  private:
    std::vector<std::byte> bytes_;
    unsigned pending_ = 0;
    unsigned used_ = 0;
};

void symbol(BitWriter& output, unsigned value) {
    if (value < 144) {
        output.huffman(value + 0x30U, 8);
    } else if (value < 256) {
        output.huffman(value - 144 + 0x190U, 9);
    } else if (value < 280) {
        output.huffman(value - 256, 7);
    } else {
        output.huffman(value - 280 + 0xC0U, 8);
    }
}

constexpr std::array<unsigned, 29> kLengthBase{3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                               15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                               67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<unsigned, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<unsigned, 30> kDistanceBase{
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<unsigned, 30> kDistanceExtra{0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                  4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                  9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

unsigned interval(std::span<const unsigned> bases, unsigned value) {
    unsigned index = 0;
    while (index + 1 < bases.size() && value >= bases[index + 1]) {
        ++index;
    }
    return index;
}

struct Match {
    unsigned length = 0;
    unsigned distance = 0;
};

void emit_match(BitWriter& output, Match match) {
    const auto length = interval(kLengthBase, match.length);
    symbol(output, 257 + length);
    output.bits(match.length - kLengthBase[length], kLengthExtra[length]);
    const auto distance = interval(kDistanceBase, match.distance);
    output.huffman(distance, 5);
    output.bits(match.distance - kDistanceBase[distance], kDistanceExtra[distance]);
}

class MatchWindow {
  public:
    explicit MatchWindow(std::span<const std::byte> bytes)
        : bytes_(bytes), heads_(65536, -1), previous_(32768, -1) {}

    void insert(std::size_t position) {
        if (position + 2 < bytes_.size()) {
            const auto index = hash(position);
            previous_[position & 32767U] = heads_[index];
            heads_[index] = static_cast<std::int64_t>(position);
        }
    }

    Match find(std::size_t position) const {
        Match best;
        if (position + 2 >= bytes_.size()) {
            return best;
        }
        auto candidate = heads_[hash(position)];
        const auto maximum = std::min<std::size_t>(258, bytes_.size() - position);
        for (unsigned attempt = 0; attempt < 128 && candidate >= 0; ++attempt) {
            const auto previous = static_cast<std::size_t>(candidate);
            if (previous >= position || position - previous > 32768) {
                break;
            }
            const auto length = match_length(position, previous, maximum);
            if (length >= 3 && length > best.length) {
                best = {length, static_cast<unsigned>(position - previous)};
                if (length == maximum) {
                    break;
                }
            }
            candidate = previous_[previous & 32767U];
        }
        return best;
    }

  private:
    unsigned hash(std::size_t position) const {
        const auto value = (std::to_integer<unsigned>(bytes_[position]) << 16) |
                           (std::to_integer<unsigned>(bytes_[position + 1]) << 8) |
                           std::to_integer<unsigned>(bytes_[position + 2]);
        return (value * 0x1E35A7BDU) >> 16;
    }

    unsigned match_length(std::size_t current, std::size_t previous, std::size_t maximum) const {
        unsigned length = 0;
        while (length < maximum && bytes_[previous + length] == bytes_[current + length]) {
            ++length;
        }
        return length;
    }

    std::span<const std::byte> bytes_;
    std::vector<std::int64_t> heads_;
    std::vector<std::int64_t> previous_;
};

} // namespace

std::vector<std::byte> deflate(std::span<const std::byte> bytes) {
    BitWriter output;
    output.bits(3, 3);
    MatchWindow window{bytes};
    for (std::size_t position = 0; position < bytes.size();) {
        const auto match = window.find(position);
        window.insert(position);
        if (match.length < 3 || window.find(position + 1).length > match.length + 1) {
            symbol(output, std::to_integer<unsigned>(bytes[position++]));
            continue;
        }
        emit_match(output, match);
        for (unsigned index = 1; index < match.length; ++index) {
            window.insert(position + index);
        }
        position += match.length;
    }
    symbol(output, 256);
    return output.finish();
}

} // namespace engine::world::storage
