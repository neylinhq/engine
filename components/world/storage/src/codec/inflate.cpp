#include "zip.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace engine::world::storage {
namespace {

class Bits {
  public:
    explicit Bits(std::span<const std::byte> data) : data_(data) {}
    std::uint32_t read(unsigned count) {
        std::uint32_t result = 0;
        for (unsigned bit = 0; bit < count; ++bit) {
            if (position_ / 8 >= data_.size()) {
                throw std::runtime_error("Truncated DEFLATE stream");
            }
            const auto byte = std::to_integer<unsigned>(data_[position_ / 8]);
            result |= ((byte >> (position_ % 8)) & 1U) << bit;
            ++position_;
        }
        return result;
    }
    void align() {
        position_ = (position_ + 7) & ~std::size_t{7};
    }

  private:
    std::span<const std::byte> data_;
    std::size_t position_ = 0;
};

struct HuffmanNode {
    std::array<int, 2> children{-1, -1};
    int symbol = -1;
};

class Huffman {
  public:
    explicit Huffman(std::span<const unsigned> lengths);
    [[nodiscard]] unsigned decode(Bits& bits) const;

  private:
    void insert(unsigned code, unsigned length, unsigned symbol);
    std::vector<HuffmanNode> nodes_{1};
};

Huffman::Huffman(std::span<const unsigned> lengths) {
    std::array<unsigned, 16> counts{};
    std::array<unsigned, 16> next{};
    for (auto length : lengths) {
        if (length > 15) {
            throw std::runtime_error("Invalid DEFLATE Huffman length");
        }
        ++counts[length];
    }
    counts[0] = 0;
    unsigned code = 0;
    for (unsigned length = 1; length <= 15; ++length) {
        code = (code + counts[length - 1]) << 1;
        next[length] = code;
        if (code + counts[length] > (1U << length)) {
            throw std::runtime_error("Oversubscribed DEFLATE Huffman tree");
        }
    }
    for (unsigned symbol = 0; symbol < lengths.size(); ++symbol) {
        const auto length = lengths[symbol];
        if (length != 0) {
            insert(next[length]++, length, symbol);
        }
    }
}

void Huffman::insert(unsigned code, unsigned length, unsigned symbol) {
    int node = 0;
    for (unsigned depth = 0; depth < length; ++depth) {
        const auto bit = (code >> (length - depth - 1)) & 1U;
        auto child = nodes_[node].children[bit];
        if (child < 0) {
            child = static_cast<int>(nodes_.size());
            nodes_[node].children[bit] = child;
            nodes_.push_back({});
        }
        node = child;
    }
    nodes_[node].symbol = static_cast<int>(symbol);
}

unsigned Huffman::decode(Bits& bits) const {
    int node = 0;
    for (unsigned depth = 0; depth < 15; ++depth) {
        node = nodes_[node].children[bits.read(1)];
        if (node < 0) {
            throw std::runtime_error("Invalid DEFLATE Huffman code");
        }
        if (nodes_[node].symbol >= 0) {
            return static_cast<unsigned>(nodes_[node].symbol);
        }
    }
    throw std::runtime_error("Overlong DEFLATE Huffman code");
}

struct Trees {
    Huffman literals;
    Huffman distances;
};

Trees fixed_trees() {
    std::array<unsigned, 288> literals{};
    for (unsigned symbol = 0; symbol < literals.size(); ++symbol) {
        if (symbol <= 143) {
            literals[symbol] = 8;
        } else if (symbol <= 255) {
            literals[symbol] = 9;
        } else if (symbol <= 279) {
            literals[symbol] = 7;
        } else {
            literals[symbol] = 8;
        }
    }
    std::array<unsigned, 32> distances{};
    distances.fill(5);
    return {Huffman{literals}, Huffman{distances}};
}

void repeat_length(Bits& bits, std::vector<unsigned>& lengths, unsigned symbol, std::size_t total) {
    unsigned value = 0;
    unsigned count = 0;
    if (symbol == 16) {
        if (lengths.empty()) {
            throw std::runtime_error("DEFLATE repeat has no previous code length");
        }
        value = lengths.back();
        count = bits.read(2) + 3;
    } else if (symbol == 17) {
        count = bits.read(3) + 3;
    } else if (symbol == 18) {
        count = bits.read(7) + 11;
    } else {
        throw std::runtime_error("Invalid DEFLATE length symbol");
    }
    if (lengths.size() + count > total) {
        throw std::runtime_error("DEFLATE repeat exceeds code table");
    }
    lengths.insert(lengths.end(), count, value);
}

Trees dynamic_trees(Bits& bits) {
    const auto literal_count = bits.read(5) + 257;
    const auto distance_count = bits.read(5) + 1;
    const auto code_count = bits.read(4) + 4;
    constexpr std::array<unsigned, 19> kOrder{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                              11, 4,  12, 3, 13, 2, 14, 1, 15};
    std::array<unsigned, 19> code_lengths{};
    for (unsigned index = 0; index < code_count; ++index) {
        code_lengths[kOrder[index]] = bits.read(3);
    }
    const Huffman code_tree{code_lengths};
    std::vector<unsigned> lengths;
    const auto total = literal_count + distance_count;
    while (lengths.size() < total) {
        const auto symbol = code_tree.decode(bits);
        if (symbol < 16) {
            lengths.push_back(symbol);
        } else {
            repeat_length(bits, lengths, symbol, total);
        }
    }
    if (lengths[256] == 0 || literal_count > 286) {
        throw std::runtime_error("DEFLATE table has no end code or too many literals");
    }
    return {Huffman{std::span{lengths}.first(literal_count)},
            Huffman{std::span{lengths}.subspan(literal_count)}};
}

struct Output {
    std::vector<std::byte> bytes;
    std::size_t limit = 0;
};

void emit(Output& output, unsigned value) {
    if (output.bytes.size() >= output.limit) {
        throw std::runtime_error("DEFLATE output exceeds declared size");
    }
    output.bytes.push_back(static_cast<std::byte>(value));
}

void back_reference(Bits& bits, const Trees& trees, Output& output, unsigned symbol) {
    constexpr std::array<unsigned, 29> kLengths{3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                67, 83, 99, 115, 131, 163, 195, 227, 258};
    constexpr std::array<unsigned, 29> kLengthBits{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    constexpr std::array<unsigned, 30> kDistances{
        1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    if (symbol < 257 || symbol > 285) {
        throw std::runtime_error("Invalid DEFLATE length code");
    }
    const auto length = kLengths[symbol - 257] + bits.read(kLengthBits[symbol - 257]);
    const auto distance_code = trees.distances.decode(bits);
    if (distance_code >= kDistances.size()) {
        throw std::runtime_error("Invalid DEFLATE distance code");
    }
    const auto extra = distance_code < 4 ? 0 : distance_code / 2 - 1;
    const auto distance = kDistances[distance_code] + bits.read(extra);
    if (distance > output.bytes.size() || output.bytes.size() + length > output.limit) {
        throw std::runtime_error("Invalid DEFLATE back reference");
    }
    for (unsigned index = 0; index < length; ++index) {
        output.bytes.push_back(output.bytes[output.bytes.size() - distance]);
    }
}

void compressed_block(Bits& bits, const Trees& trees, Output& output) {
    while (true) {
        const auto symbol = trees.literals.decode(bits);
        if (symbol == 256) {
            return;
        }
        if (symbol < 256) {
            emit(output, symbol);
        } else {
            back_reference(bits, trees, output, symbol);
        }
    }
}

void stored_block(Bits& bits, Output& output) {
    bits.align();
    const auto length = bits.read(16);
    const auto inverse = bits.read(16);
    if ((length ^ inverse) != 0xFFFFU) {
        throw std::runtime_error("Invalid DEFLATE stored-block length");
    }
    for (unsigned index = 0; index < length; ++index) {
        emit(output, bits.read(8));
    }
}

} // namespace

std::vector<std::byte> inflate(std::span<const std::byte> compressed, std::size_t expected_size) {
    Bits bits{compressed};
    Output output{{}, expected_size};
    output.bytes.reserve(std::min<std::size_t>(expected_size, 1048576));
    bool final = false;
    while (!final) {
        final = bits.read(1) != 0;
        const auto type = bits.read(2);
        if (type == 0) {
            stored_block(bits, output);
        } else if (type == 1) {
            compressed_block(bits, fixed_trees(), output);
        } else if (type == 2) {
            compressed_block(bits, dynamic_trees(bits), output);
        } else {
            throw std::runtime_error("Reserved DEFLATE block type");
        }
    }
    if (output.bytes.size() != expected_size) {
        throw std::runtime_error("DEFLATE output size does not match ZIP directory");
    }
    return std::move(output.bytes);
}

} // namespace engine::world::storage
