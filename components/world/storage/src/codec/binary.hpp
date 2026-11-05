#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::world::storage {

class Reader {
  public:
    explicit Reader(std::span<const std::byte> data);
    [[nodiscard]] std::size_t remaining() const;
    [[nodiscard]] std::size_t position() const;
    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint8_t peek() const;
    [[nodiscard]] std::uint16_t u16();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::uint64_t u64();
    [[nodiscard]] std::int32_t i32();
    [[nodiscard]] float f32();
    [[nodiscard]] std::int32_t big_i32();
    [[nodiscard]] std::string string(std::size_t count);
    [[nodiscard]] std::string terminated();
    [[nodiscard]] std::span<const std::byte> take(std::size_t count);

  private:
    std::span<const std::byte> data_;
    std::size_t position_ = 0;
};

class Writer {
  public:
    void u8(std::uint8_t value);
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void i32(std::int32_t value);
    void f32(float value);
    void string(const std::string& value);
    void terminated(const std::string& value);
    void append(std::span<const std::byte> bytes);
    [[nodiscard]] const std::vector<std::byte>& bytes() const;

  private:
    std::vector<std::byte> bytes_;
};

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, std::span<const std::byte> bytes);
[[nodiscard]] std::filesystem::path utf8_path(std::string_view text);
[[nodiscard]] std::string path_text(const std::filesystem::path& path);
[[nodiscard]] std::string extension_text(const std::filesystem::path& path);

} // namespace engine::world::storage
