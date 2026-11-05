#pragma once

#include "binary.hpp"

namespace engine::world::storage {

[[nodiscard]] std::uint32_t checksum_crc32(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> zip_extract(std::span<const std::byte> archive,
                                                 const std::string& name);
[[nodiscard]] std::vector<std::byte> zip_compress(std::span<const std::byte> bytes,
                                                  const std::string& name);
[[nodiscard]] std::vector<std::byte> inflate(std::span<const std::byte> compressed,
                                             std::size_t expected_size);
[[nodiscard]] std::vector<std::byte> deflate(std::span<const std::byte> bytes);

} // namespace engine::world::storage
