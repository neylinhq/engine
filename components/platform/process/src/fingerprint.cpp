#include <array>
#include <engine/platform/process/process.hpp>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <stdexcept>
namespace engine::platform::process {
namespace {
using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
Context context() {
    Context result(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!result || EVP_DigestInit_ex(result.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("Cannot initialize SHA-256");
    }
    return result;
}
void update(EVP_MD_CTX* digest, std::span<const std::byte> bytes) {
    if (EVP_DigestUpdate(digest, bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("Cannot update SHA-256");
    }
}
std::string finish(EVP_MD_CTX* digest) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned size = 0;
    if (EVP_DigestFinal_ex(digest, bytes.data(), &size) != 1 || size != 32) {
        throw std::runtime_error("Cannot finalize SHA-256");
    }
    constexpr std::string_view hex = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (unsigned index = 0; index < size; ++index) {
        result += hex[bytes[index] >> 4];
        result += hex[bytes[index] & 15];
    }
    return result;
}
} // namespace
std::string sha256(std::span<const std::byte> bytes) {
    auto digest = context();
    update(digest.get(), bytes);
    return finish(digest.get());
}
std::string file_sha256(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot fingerprint file: " + path_text(path));
    }
    auto digest = context();
    std::array<char, 65536> bytes{};
    while (file.read(bytes.data(), bytes.size()) || file.gcount() != 0) {
        update(digest.get(),
               std::as_bytes(std::span(bytes).first(static_cast<std::size_t>(file.gcount()))));
    }
    if (!file.eof()) {
        throw std::runtime_error("Cannot read file: " + path_text(path));
    }
    return finish(digest.get());
}
} // namespace engine::platform::process
