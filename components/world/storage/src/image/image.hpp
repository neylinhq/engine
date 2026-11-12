#ifndef ENGINE_WORLD_STORAGE_SRC_IMAGE_IMAGE_HPP
#define ENGINE_WORLD_STORAGE_SRC_IMAGE_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace engine::world::storage {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> pixels;
};

struct TextureCoordinate {
    double u = 0;
    double v = 0;
};

[[nodiscard]] Image decode_jpeg(std::span<const std::uint8_t> bytes);
[[nodiscard]] Image decode_png(std::span<const std::byte> bytes);
[[nodiscard]] Image decode_bmp(std::span<const std::byte> bytes);
[[nodiscard]] Image decode_gif(std::span<const std::byte> bytes);
[[nodiscard]] Image decode_ppm(std::span<const std::byte> bytes);
[[nodiscard]] std::shared_ptr<const Image> load_image(const std::filesystem::path& path);
[[nodiscard]] std::uint32_t sample_image(const Image& image, TextureCoordinate coordinate);

} // namespace engine::world::storage

#endif
