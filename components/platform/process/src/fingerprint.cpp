#include <array>
#include <engine/platform/process/process.hpp>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <algorithm>
#include <limits>
#include <windows.h>

#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif
namespace engine::platform::process {
namespace {
using Digest = std::array<unsigned char, 32>;

#ifdef _WIN32
using Context = std::unique_ptr<void, decltype(&BCryptDestroyHash)>;
Context context() {
    BCRYPT_HASH_HANDLE handle{};
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &handle, nullptr, 0, nullptr, 0, 0) < 0) {
        throw std::runtime_error("Cannot initialize SHA-256");
    }
    return Context(handle, &BCryptDestroyHash);
}
void update(BCRYPT_HASH_HANDLE digest, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto count = std::min(bytes.size(), std::size_t{std::numeric_limits<ULONG>::max()});
        // The CNG signature takes a mutable pointer for this input-only buffer.
        auto* data =
            const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(bytes.data()));
        if (BCryptHashData(digest, data, static_cast<ULONG>(count), 0) < 0) {
            throw std::runtime_error("Cannot update SHA-256");
        }
        bytes = bytes.subspan(count);
    }
}
Digest finish(BCRYPT_HASH_HANDLE digest) {
    Digest bytes{};
    if (BCryptFinishHash(digest, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0) {
        throw std::runtime_error("Cannot finalize SHA-256");
    }
    return bytes;
}
#else
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
Digest finish(EVP_MD_CTX* digest) {
    Digest bytes{};
    unsigned size = 0;
    if (EVP_DigestFinal_ex(digest, bytes.data(), &size) != 1 || size != bytes.size()) {
        throw std::runtime_error("Cannot finalize SHA-256");
    }
    return bytes;
}
#endif

std::string to_hex(const Digest& bytes) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result += kHex[byte >> 4];
        result += kHex[byte & 15];
    }
    return result;
}
} // namespace
std::string sha256(std::span<const std::byte> bytes) {
    auto digest = context();
    update(digest.get(), bytes);
    return to_hex(finish(digest.get()));
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
    return to_hex(finish(digest.get()));
}
} // namespace engine::platform::process
