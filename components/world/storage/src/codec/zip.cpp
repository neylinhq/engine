#include "zip.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace engine::world::storage {
namespace {

std::uint32_t crc32(std::span<const std::byte> bytes) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> result{};
        for (unsigned index = 0; index < 256; ++index) {
            auto value = index;
            for (unsigned bit = 0; bit < 8; ++bit) {
                value = (value >> 1) ^ ((value & 1) != 0 ? 0xEDB88320U : 0U);
            }
            result[index] = value;
        }
        return result;
    }();
    std::uint32_t result = 0xFFFFFFFFU;
    for (auto byte : bytes) {
        result = (result >> 8) ^ table[(result ^ std::to_integer<unsigned>(byte)) & 255U];
    }
    return result ^ 0xFFFFFFFFU;
}

struct Entry {
    std::string name;
    unsigned flags = 0;
    unsigned method = 0;
    std::uint32_t checksum = 0;
    std::uint64_t compressed = 0;
    std::uint64_t uncompressed = 0;
    std::uint64_t offset = 0;
};

void zip64_extra(Entry& entry, std::span<const std::byte> bytes) {
    Reader reader{bytes};
    while (reader.remaining() >= 4) {
        const auto type = reader.u16();
        const auto length = reader.u16();
        Reader extra{reader.take(length)};
        if (type != 1) {
            continue;
        }
        if (entry.uncompressed == 0xFFFFFFFFU) {
            entry.uncompressed = extra.u64();
        }
        if (entry.compressed == 0xFFFFFFFFU) {
            entry.compressed = extra.u64();
        }
        if (entry.offset == 0xFFFFFFFFU) {
            entry.offset = extra.u64();
        }
    }
}

Entry directory_entry(Reader& reader) {
    if (reader.u32() != 0x02014B50U) {
        throw std::runtime_error("Invalid ZIP central-directory signature");
    }
    static_cast<void>(reader.take(4));
    Entry result;
    result.flags = reader.u16();
    result.method = reader.u16();
    static_cast<void>(reader.take(4));
    result.checksum = reader.u32();
    result.compressed = reader.u32();
    result.uncompressed = reader.u32();
    const auto name_length = reader.u16();
    const auto extra_length = reader.u16();
    const auto comment_length = reader.u16();
    static_cast<void>(reader.take(8));
    result.offset = reader.u32();
    result.name = reader.string(name_length);
    zip64_extra(result, reader.take(extra_length));
    static_cast<void>(reader.take(comment_length));
    return result;
}

std::size_t zip64_directory(std::span<const std::byte> archive, std::size_t end_offset) {
    if (end_offset < 20) {
        throw std::runtime_error("Missing ZIP64 directory locator");
    }
    Reader locator{archive.subspan(end_offset - 20)};
    if (locator.u32() != 0x07064B50U) {
        throw std::runtime_error("Missing ZIP64 directory locator");
    }
    static_cast<void>(locator.u32());
    const auto position = locator.u64();
    if (position > archive.size() || archive.size() - position < 56) {
        throw std::runtime_error("ZIP64 directory locator lies outside archive");
    }
    Reader end{archive.subspan(static_cast<std::size_t>(position))};
    if (end.u32() != 0x06064B50U) {
        throw std::runtime_error("Invalid ZIP64 end record");
    }
    static_cast<void>(end.take(44));
    return static_cast<std::size_t>(end.u64());
}

std::size_t directory_offset(std::span<const std::byte> archive) {
    if (archive.size() < 22) {
        throw std::runtime_error("Truncated ZIP archive");
    }
    const auto minimum = archive.size() > 65557 ? archive.size() - 65557 : 0;
    for (auto offset = archive.size() - 22;; --offset) {
        Reader reader{archive.subspan(offset)};
        if (reader.u32() == 0x06054B50U) {
            static_cast<void>(reader.take(12));
            const auto directory = reader.u32();
            const auto comment = reader.u16();
            if (reader.remaining() == comment) {
                return directory != 0xFFFFFFFFU ? directory : zip64_directory(archive, offset);
            }
        }
        if (offset == minimum) {
            break;
        }
    }
    throw std::runtime_error("ZIP end-of-directory not found");
}

std::vector<std::byte> extract(std::span<const std::byte> archive, const Entry& entry) {
    if ((entry.flags & 1) != 0 || entry.uncompressed > 8ULL * 1024 * 1024 * 1024 ||
        entry.offset >= archive.size()) {
        throw std::runtime_error("Encrypted, oversized or invalid ZIP entry");
    }
    Reader local{archive.subspan(static_cast<std::size_t>(entry.offset))};
    if (local.u32() != 0x04034B50U) {
        throw std::runtime_error("Invalid ZIP local header");
    }
    static_cast<void>(local.take(22));
    const auto name_length = local.u16();
    const auto extra_length = local.u16();
    static_cast<void>(local.take(name_length + extra_length));
    const auto bytes = local.take(static_cast<std::size_t>(entry.compressed));
    std::vector<std::byte> result;
    if (entry.method == 0) {
        result.assign(bytes.begin(), bytes.end());
    } else if (entry.method == 8) {
        result = inflate(bytes, static_cast<std::size_t>(entry.uncompressed));
    } else {
        throw std::runtime_error("Unsupported CVox ZIP compression method");
    }
    if (result.size() != entry.uncompressed || crc32(result) != entry.checksum) {
        throw std::runtime_error("ZIP entry checksum or size mismatch");
    }
    return result;
}

struct ZipDescription {
    std::size_t size = 0;
    std::size_t compressed = 0;
    unsigned method = 0;
    std::string name;
    std::uint32_t checksum = 0;
    bool large = false;
};

void common_header(Writer& writer, const ZipDescription& description) {
    writer.u16(description.large ? 45 : 20);
    writer.u16(0);
    writer.u16(static_cast<std::uint16_t>(description.method));
    writer.u16(0);
    writer.u16(0);
    writer.u32(description.checksum);
    writer.u32(description.large ? 0xFFFFFFFFU
                                 : static_cast<std::uint32_t>(description.compressed));
    writer.u32(description.large ? 0xFFFFFFFFU : static_cast<std::uint32_t>(description.size));
    writer.u16(static_cast<std::uint16_t>(description.name.size()));
    writer.u16(description.large ? 20 : 0);
}

void zip64_sizes(Writer& writer, const ZipDescription& description) {
    if (description.large) {
        writer.u16(1);
        writer.u16(16);
        writer.u64(description.size);
        writer.u64(description.compressed);
    }
}

void end_record(Writer& writer, std::uint64_t offset, std::uint64_t size, bool large) {
    if (large) {
        const auto zip64_offset = writer.bytes().size();
        writer.u32(0x06064B50U);
        writer.u64(44);
        writer.u16(45);
        writer.u16(45);
        writer.u32(0);
        writer.u32(0);
        writer.u64(1);
        writer.u64(1);
        writer.u64(size);
        writer.u64(offset);
        writer.u32(0x07064B50U);
        writer.u32(0);
        writer.u64(zip64_offset);
        writer.u32(1);
    }
    writer.u32(0x06054B50U);
    writer.u16(0);
    writer.u16(0);
    writer.u16(large ? 65535 : 1);
    writer.u16(large ? 65535 : 1);
    writer.u32(large ? 0xFFFFFFFFU : static_cast<std::uint32_t>(size));
    writer.u32(large ? 0xFFFFFFFFU : static_cast<std::uint32_t>(offset));
    writer.u16(0);
}

} // namespace

std::uint32_t checksum_crc32(std::span<const std::byte> bytes) {
    return crc32(bytes);
}

std::vector<std::byte> zip_extract(std::span<const std::byte> archive, const std::string& name) {
    const auto offset = directory_offset(archive);
    if (offset >= archive.size()) {
        throw std::runtime_error("ZIP directory lies outside file");
    }
    Reader directory{archive.subspan(offset)};
    while (directory.remaining() >= 46) {
        const auto entry = directory_entry(directory);
        if (entry.name == name) {
            return extract(archive, entry);
        }
    }
    throw std::runtime_error("ZIP does not contain expected entry: " + name);
}

std::vector<std::byte> zip_compress(std::span<const std::byte> bytes, const std::string& name) {
    if (name.size() > 65535) {
        throw std::runtime_error("ZIP file name is too long");
    }
    const auto compressed = deflate(bytes);
    const auto payload = compressed.size() < bytes.size() ? std::span{compressed} : bytes;
    const ZipDescription description{
        bytes.size(), payload.size(), compressed.size() < bytes.size() ? 8U : 0U,
        name,         crc32(bytes),   bytes.size() > 0xFFFF0000U};
    Writer writer;
    writer.u32(0x04034B50U);
    common_header(writer, description);
    writer.string(name);
    zip64_sizes(writer, description);
    writer.append(payload);
    const auto central_offset = writer.bytes().size();
    writer.u32(0x02014B50U);
    writer.u16(description.large ? 45 : 20);
    common_header(writer, description);
    writer.u16(0);
    writer.u16(0);
    writer.u16(0);
    writer.u32(0);
    writer.u32(0);
    writer.string(name);
    zip64_sizes(writer, description);
    const auto central_size = writer.bytes().size() - central_offset;
    end_record(writer, central_offset, central_size, description.large);
    return writer.bytes();
}

} // namespace engine::world::storage
